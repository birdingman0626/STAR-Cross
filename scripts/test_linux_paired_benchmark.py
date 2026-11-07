import unittest
from unittest import mock

import linux_paired_benchmark as bench


class HostLoadTest(unittest.TestCase):
    def test_host_background_subtracts_vm_cpu(self):
        # 10 s on 20 host CPUs, 25% busy = 50 CPU-s; the VM used 30 CPU-s -> 20 CPU-s of 200 = 10%
        before, after = (0, 0, 20), (int(7.5e7), int(1e8), 20)
        self.assertAlmostEqual(bench.host_background_percent(before, after, 30.0), 10.0)

    def test_host_background_is_never_negative(self):
        before, after = (0, 0, 20), (int(9e7), int(1e8), 20)
        self.assertEqual(bench.host_background_percent(before, after, 50.0), 0.0)

    def test_host_background_rejects_empty_interval(self):
        self.assertIsNone(bench.host_background_percent((5, 7, 20), (5, 7, 20), 0.0))

    def test_windows_host_cpu_parses_and_fails_closed(self):
        done = mock.Mock(stdout="3987275109375 134385433534374046 20\r\n")
        with mock.patch.object(bench.subprocess, "run", return_value=done):
            self.assertEqual(bench.windows_host_cpu(), (3987275109375, 134385433534374046, 20))
        with mock.patch.object(bench.subprocess, "run", return_value=mock.Mock(stdout="Get-CimInstance : error")):
            self.assertIsNone(bench.windows_host_cpu())
        with mock.patch.object(bench.subprocess, "run", side_effect=FileNotFoundError):
            self.assertIsNone(bench.windows_host_cpu())


class EnvironmentGateTest(unittest.TestCase):
    def run_with(self, vm, host="absent"):
        run = {"background_cpu_mean_percent": vm}
        if host != "absent":
            run["host_background_cpu_mean_percent"] = host
        return run

    def test_linux_gate_ignores_host(self):
        self.assertTrue(bench.environment_gate([self.run_with(1.0)], 5.0))
        self.assertFalse(bench.environment_gate([self.run_with(6.0)], 5.0))

    def test_wsl_gate_needs_known_quiet_host(self):
        self.assertTrue(bench.environment_gate([self.run_with(1.0, 2.0)], 5.0, 5.0))
        self.assertFalse(bench.environment_gate([self.run_with(1.0, 7.0)], 5.0, 5.0))
        self.assertFalse(bench.environment_gate([self.run_with(1.0, None)], 5.0, 5.0))
        self.assertFalse(bench.environment_gate([self.run_with(1.0)], 5.0, 5.0))


if __name__ == "__main__":
    unittest.main()
