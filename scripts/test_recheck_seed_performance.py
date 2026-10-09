import unittest
from recheck_seed_performance import median_ratio,quiet_enough


class ReplayRecheckTests(unittest.TestCase):
    def test_uses_paired_ratio_not_ratio_of_unpaired_medians(self):
        runs=[dict(cpu_seconds=1,candidate_seconds=2),dict(cpu_seconds=100,candidate_seconds=50),
              dict(cpu_seconds=2,candidate_seconds=1)]
        self.assertEqual(median_ratio(runs),.5)

    def test_rejects_cpu_pressure(self):
        self.assertFalse(quiet_enough([11,20,99],100*1024**3))

    def test_rejects_memory_pressure_even_with_idle_cpu(self):
        self.assertFalse(quiet_enough([1,2,3],39*1024**3))

    def test_operational_gate_boundary(self):
        self.assertTrue(quiet_enough([9,10,11],40*1024**3))


if __name__=='__main__':unittest.main()
