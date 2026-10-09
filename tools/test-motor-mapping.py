"""Check firmware pin calibration against the user's measured wheel signatures.

This validates the derived mapping, not actual hardware or roller mounting.
Run: python tools/test-motor-mapping.py
"""
import re
import unittest
from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / 'mecanum-bot.ino').read_text(encoding='utf-8')
WHEELS = ('FL', 'FR', 'RL', 'RR')
OLD_PINS = ((5, 18), (19, 21), (2, 4), (16, 17))
# Reported physical wheel signs for Y+, turn+, X+ (forward=+1).
MEASURED = ((-1, -1, 1), (1, -1, 1), (-1, 1, 1), (1, 1, 1))

def mix(y, x, turn):
    return (y + x + turn, y - x - turn, y - x + turn, y + x - turn)

def mapping_from_measurements():
    signatures = list(zip(mix(1, 0, 0), mix(0, 0, 1), mix(0, 1, 0)))
    mapping = []
    for measured in MEASURED:
        matches = [(index, polarity) for index, signature in enumerate(signatures)
                   for polarity in (1, -1)
                   if tuple(polarity * value for value in signature) == measured]
        assert len(matches) == 1, matches
        mapping.append(matches[0])
    return mapping

def physical_wheels(y, x, turn):
    logical = mix(y, x, turn)
    electrical = [0] * 4
    for wheel, value in zip(WHEELS, logical):
        pins = tuple(int(re.search(rf'const int {wheel}_IN{i} = (\d+);', SOURCE)[1]) for i in (1, 2))
        for index, old_pins in enumerate(OLD_PINS):
            if pins == old_pins:
                electrical[index] = value
                break
            if pins == old_pins[::-1]:
                electrical[index] = -value
                break
        else:
            raise AssertionError(f'Unrecognized motor pins: {wheel} {pins}')
    return tuple(polarity * electrical[index] for index, polarity in mapping_from_measurements())

def stick(number, direction):
    body = re.search(rf'void mapStick{number}\([^{{]+\) \{{(.*?)\n\}}', SOURCE, re.S)[1]
    case = re.search(rf'case {direction}: (.*?)break;', body)
    values = {'y': 0, 'x': 0, 't': 0}
    if case:
        for axis, sign in re.findall(r'([yxt]) = (-?)strength;', case[1]):
            values[axis] = -1 if sign else 1
    return values['y'], values['x'], values['t']

class MotorCalibration(unittest.TestCase):
    def test_unique_mapping(self):
        self.assertEqual(mapping_from_measurements(), [(2, -1), (3, 1), (1, -1), (0, 1)])

    def test_sticks_match_drawing_through_reported_wiring(self):
        expected = {
            1: {1: (1, 1, 1, 1), 2: (2, 0, 2, 0), 3: (1, -1, 1, -1),
                4: (0, -2, 0, -2), 5: (-1, -1, -1, -1), 6: (-2, 0, -2, 0),
                7: (-1, 1, -1, 1), 8: (0, 2, 0, 2)},
            2: {1: (0, 0, 0, 0), 2: (2, 0, 0, 2), 3: (1, -1, -1, 1),
                4: (0, -2, -2, 0), 5: (0, 0, 0, 0), 6: (-2, 0, 0, -2),
                7: (-1, 1, 1, -1), 8: (0, 2, 2, 0)},
        }
        for number, directions in expected.items():
            for direction, wheels in directions.items():
                with self.subTest(stick=number, direction=direction):
                    self.assertEqual(physical_wheels(*stick(number, direction)), wheels)

if __name__ == '__main__':
    unittest.main()
