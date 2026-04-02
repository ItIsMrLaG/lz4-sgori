#!/usr/bin/env python3
"""
Experimental environment for comparing LZ4 compression variations in Linux kernel.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List

# Import graph generator directly
from generate_graphs import GraphGenerator


@dataclass
class LZ4Stats:
    """Container for LZ4 I/O statistics."""

    # Read statistics
    stats_r_reqs_total: int = 0
    stats_r_reqs_failed: int = 0
    stats_r_segments: int = 0
    stats_r_decomp_size: int = 0
    stats_r_comp_size: int = 0
    stats_r_copy_ns: int = 0
    stats_r_comp_ns: int = 0
    stats_r_decomp_ns: int = 0
    stats_r_total_ns: int = 0

    # Write statistics
    stats_w_reqs_total: int = 0
    stats_w_reqs_failed: int = 0
    stats_w_segments: int = 0
    stats_w_decomp_size: int = 0
    stats_w_comp_size: int = 0
    stats_w_copy_ns: int = 0
    stats_w_comp_ns: int = 0
    stats_w_decomp_ns: int = 0
    stats_w_total_ns: int = 0

    @classmethod
    def from_sysfs(cls, base_path: Path = Path("/sys/module/lz4e_bdev/parameters")):
        """Read statistics from sysfs."""
        stats = cls()
        for field in cls.__dataclass_fields__:
            param_path = base_path / field
            if param_path.exists():
                with open(param_path) as f:
                    value = f.read().strip()
                    try:
                        setattr(stats, field, int(value))
                    except ValueError:
                        pass
        return stats

    def to_dict(self) -> Dict:
        """Convert to dictionary."""
        return asdict(self)


class LZ4Experiment:
    """Main experiment class for LZ4 compression testing."""

    COMPRESSION_TYPES = ["cont", "vect", "strm", "extd"]
    IGNORE_FILES = [".gitkeep"]

    def __init__(self, args):
        self.args = args
        self.bs_bytes = self._parse_bs(args.bs)
        self.proxy_dev = Path("/dev/lz4e0")
        self.under_dev = args.under_dev
        self.tmp_dir = Path("./tmp")
        self.tmp_dir.mkdir(exist_ok=True)

        # Create result and graph directories
        self.result_dir = Path(args.result)
        self.graph_dir = Path(args.graph)
        self.result_dir.mkdir(parents=True, exist_ok=True)
        self.graph_dir.mkdir(parents=True, exist_ok=True)

        # Setup dataset
        self.dataset_dir = Path(args.dataset)
        if not self.dataset_dir.exists():
            raise FileNotFoundError(f"Dataset directory not found: {self.dataset_dir}")

    def _parse_bs(self, bs_str: str) -> int:
        """Parse block size from IEC format to bytes."""
        try:
            # Use numfmt utility for conversion
            result = subprocess.run(
                ["numfmt", "--from=iec", bs_str],
                capture_output=True,
                text=True,
                check=True,
            )
            return int(result.stdout.strip())
        except subprocess.CalledProcessError:
            # Fallback to manual parsing
            multipliers = {"K": 1024, "M": 1024**2, "G": 1024**3}
            if bs_str[-1] in multipliers:
                return int(bs_str[:-1]) * multipliers[bs_str[-1]]
            return int(bs_str)

    def _setup_under_device(self):
        """Setup underlying device if not provided."""
        if self.under_dev == "None" or self.under_dev is None:
            dev_size_kb = self.args.dev_size
            print(f"Creating RAM device of size {dev_size_kb} KB...")
            subprocess.run(
                ["modprobe", "brd", "rd_nr=1", f"rd_size={dev_size_kb}", "max_part=0"],
                check=True,
            )
            self.under_dev = "/dev/ram0"
            time.sleep(1)  # Wait for device to be created
        return self.under_dev

    def _load_modules(self):
        """Load required kernel modules."""
        print("Loading lz4e_bdev module...")
        subprocess.run(["make", "reinsert"], cwd=".", check=True)
        time.sleep(1)  # Wait for module to initialize

    def _unload_modules(self):
        """Unload kernel modules."""
        print("Unloading modules...")
        subprocess.run(["make", "remove"], cwd=".", check=True)

    def _create_proxy_device(self, under_dev: str):
        """Create proxy device over underlying device."""
        print(f"Creating proxy device over {under_dev}...")
        sysfs_param = Path("/sys/module/lz4e_bdev/parameters/mapper")
        with open(sysfs_param, "w") as f:
            f.write(under_dev)
        time.sleep(1)  # Wait for device to be created

        if not self.proxy_dev.exists():
            raise RuntimeError(f"Proxy device {self.proxy_dev} not created")

    def _remove_proxy_device(self):
        """Remove proxy device."""
        print("Removing proxy device...")
        sysfs_param = Path("/sys/module/lz4e_bdev/parameters/unmapper")
        with open(sysfs_param, "w") as f:
            f.write("lz4e0")
        time.sleep(1)

    def _set_compression_type(self, comp_type: str):
        """Set compression type."""
        sysfs_param = Path("/sys/module/lz4e_bdev/parameters/comp_type")
        with open(sysfs_param, "w") as f:
            f.write(comp_type)

    def _set_acceleration(self, accel: int):
        """Set acceleration factor."""
        sysfs_param = Path("/sys/module/lz4e_bdev/parameters/acceleration")
        with open(sysfs_param, "w") as f:
            f.write(str(accel))

    def _reset_stats(self):
        """Reset I/O statistics."""
        sysfs_param = Path("/sys/module/lz4e_bdev/parameters/stats_reset")
        with open(sysfs_param, "w") as f:
            f.write("1")

    def _get_count(self, file_size: int) -> int:
        """Calculate count parameter for dd."""
        return (file_size // self.bs_bytes) + 1

    def _run_dd_write(self, input_file: Path, count: int) -> bool:
        """Run dd write operation."""
        cmd = [
            "dd",
            f"if={input_file}",
            f"of={self.proxy_dev}",
            f"bs={self.args.bs}",
            f"count={count}",
            "oflag=direct",
            "status=none",
        ]
        result = subprocess.run(cmd, capture_output=True)
        return result.returncode == 0

    def _run_dd_read(self, output_file: Path, count: int) -> bool:
        """Run dd read operation."""
        cmd = [
            "dd",
            f"if={self.proxy_dev}",
            f"of={output_file}",
            f"bs={self.args.bs}",
            f"count={count}",
            "iflag=direct",
            "status=none",
        ]
        result = subprocess.run(cmd, capture_output=True)
        return result.returncode == 0

    def _verify_integrity(self, original: Path, test: Path, size: int) -> bool:
        """Verify integrity by comparing first N bytes."""
        with open(original, "rb") as f1, open(test, "rb") as f2:
            return f1.read(size) == f2.read(size)

    def _get_test_files(self) -> List[Path]:
        """Recursively find all test files in dataset directory."""
        test_files = []
        for root, _, files in os.walk(self.dataset_dir):
            for file in files:
                if file not in self.IGNORE_FILES:
                    test_files.append(Path(root) / file)
        return test_files

    def _save_intermediate_results(
        self, test_file: Path, comp_type: str, run_num: int, stats: LZ4Stats
    ):
        """Save intermediate results to JSON file."""
        # Create directory structure
        comp_dir = self.result_dir / comp_type
        comp_dir.mkdir(exist_ok=True)

        # Create filename with test file name and run number
        safe_filename = test_file.name.replace("/", "_")
        result_file = comp_dir / f"{safe_filename}_run{run_num}.json"

        # Prepare result data
        result_data = {
            "test_file": str(test_file),
            "compression_type": comp_type,
            "run_number": run_num,
            "block_size": self.args.bs,
            "acceleration": self.args.acceleration,
            "statistics": stats.to_dict(),
        }

        # Save to JSON
        with open(result_file, "w") as f:
            json.dump(result_data, f, indent=2)

        print(f"  Saved intermediate results to {result_file}")

    def run_single_test(self, test_file: Path, comp_type: str, run_num: int):
        """Run a single test for a specific file and compression type."""
        file_size = test_file.stat().st_size
        count = self._get_count(file_size)

        # Create temporary output file
        with tempfile.NamedTemporaryFile(dir=self.tmp_dir, delete=False) as tmp:
            tmp_output = Path(tmp.name)

        try:
            # Set compression parameters
            self._set_compression_type(comp_type)
            self._set_acceleration(self.args.acceleration)
            self._reset_stats()

            # Write test file to proxy device
            print(f"    Writing {test_file.name} to proxy device...")
            if not self._run_dd_write(test_file, count):
                raise RuntimeError(f"DD write failed for {test_file}")

            # Read from proxy device
            print(f"    Reading from proxy device to {tmp_output.name}...")
            if not self._run_dd_read(tmp_output, count):
                raise RuntimeError(f"DD read failed for {test_file}")

            # Verify integrity
            if not self._verify_integrity(test_file, tmp_output, file_size):
                raise RuntimeError(f"Integrity check failed for {test_file}")

            # Collect statistics
            stats = LZ4Stats.from_sysfs()

            # Save intermediate results
            self._save_intermediate_results(test_file, comp_type, run_num, stats)

            print("    Test completed successfully")

        except Exception as e:
            print(f"    ERROR: {e}")
            # Still try to save any available statistics
            try:
                stats = LZ4Stats.from_sysfs()
                self._save_intermediate_results(test_file, comp_type, run_num, stats)
            except e:
                pass
            raise

        finally:
            # Cleanup
            if tmp_output.exists():
                tmp_output.unlink()
            self._reset_stats()

    def _generate_graphs(self):
        """Generate graphs using imported GraphGenerator."""
        print("\n" + "=" * 60)
        print("Generating performance graphs...")
        print("=" * 60)

        try:
            # Create graph generator instance
            generator = GraphGenerator(self.result_dir, self.graph_dir)

            # Generate all graphs
            generator.generate_all_graphs()

            print("\nGraphs generated successfully!")
            print(f"Graphs saved to: {self.graph_dir}")

        except Exception as e:
            print(f"\nError generating graphs: {e}")
            import traceback

            traceback.print_exc()

    def run_experiment(self):
        """Run the complete experiment."""
        experiment_success = False

        try:
            # Setup environment
            under_dev = self._setup_under_device()
            self._load_modules()
            self._create_proxy_device(under_dev)

            # Get all test files
            test_files = self._get_test_files()
            print(f"\nFound {len(test_files)} test files in dataset")

            # Run experiments
            for test_file in test_files:
                print(f"\nProcessing test file: {test_file}")
                file_size = test_file.stat().st_size
                print(f"  File size: {file_size} bytes")

                for comp_type in self.COMPRESSION_TYPES:
                    print(f"\n  Testing compression type: {comp_type}")

                    for run_num in range(1, self.args.runs + 1):
                        print(f"    Run {run_num}/{self.args.runs}")
                        self.run_single_test(test_file, comp_type, run_num)

            experiment_success = True
            print("\nExperiment completed successfully!")

        except Exception as e:
            print(f"\nExperiment failed: {e}")
            import traceback

            traceback.print_exc()

        finally:
            # Always try to generate graphs from available data
            self._generate_graphs()

            # Cleanup
            try:
                self._remove_proxy_device()
            except Exception as e:
                print(f"Warning: Could not remove proxy device: {e}")

            try:
                self._unload_modules()
            except Exception as e:
                print(f"Warning: Could not unload modules: {e}")

        if not experiment_success:
            sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="LZ4 Compression Experiment")
    parser.add_argument("--bs", default="1M", help="Block size (IEC format)")
    parser.add_argument(
        "--dataset", default="./experiment/dataset", help="Path to dataset directory"
    )
    parser.add_argument(
        "--result",
        default="./experiment/result",
        help="Path to intermediate results directory",
    )
    parser.add_argument("--graph", default="./experiment/graph", help="Path to graph directory")
    parser.add_argument(
        "--under-dev", default="None", help="Underlying device (or None for RAM device)"
    )
    parser.add_argument(
        "--dev-size",
        default=1024 * 1024,
        type=int,
        help="Device size in KB (for RAM device)",
    )
    parser.add_argument("--runs", default=5, type=int, help="Number of test runs")
    parser.add_argument("--acceleration", default=1, type=int, help="LZ4 acceleration factor")
    parser.add_argument("--no-graphs", action="store_true", help="Skip graph generation")

    args = parser.parse_args()

    # Override graph generation if requested
    if args.no_graphs:
        # Monkey patch the graph generation method
        def no_op(self):
            print("Graph generation skipped (--no-graphs)")

        LZ4Experiment._generate_graphs = no_op

    experiment = LZ4Experiment(args)
    experiment.run_experiment()


if __name__ == "__main__":
    main()
