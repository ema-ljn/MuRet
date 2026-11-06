"""El informe describe el movimiento registrado, sin una conclusión fija."""
import copy
import unittest

from analyze_cl7_series import report


class ReportTest(unittest.TestCase):
    def setUp(self):
        # Resumen sintético de presentación: no es evidencia del original.
        self.result = {
            'timing': {'first_to_last_start_ms': 89969, 'observer_rate_hz': 25.0,
                       'core_read_ms': {'min': 0, 'max': 15}},
            'hero': {'unique_positions': 1, 'position_changes_observed': 0,
                     'unique_visual_cells': 1, 'action_counts': {'01': 2250},
                     'movement_counts': {'00': 2250}, 'path_num_counts': {'5': 2250}},
            'entities': {'observations': 90, 'bird_alive_counts': {'5': 90},
                         'fish_alive_counts': {'0': 90}},
            'boundary_unstable': {'count': 0}, 'start': {}, 'end': {},
            'screenshot_frames_in_metadata': 100, 'limits': [],
        }

    def test_stationary_capture_does_not_claim_a_walk(self):
        self.assertIn('No se observo desplazamiento', report(self.result))

    def test_changed_positions_are_reported_as_motion(self):
        moving = copy.deepcopy(self.result)
        moving['hero'].update(unique_positions=63, position_changes_observed=62,
                              unique_visual_cells=10)
        text = report(moving)
        self.assertIn('Se observo desplazamiento', text)
        self.assertNotIn('sin traslado observado', text)
        self.assertIn('cambios observados:62', text)


if __name__ == '__main__':
    unittest.main()
