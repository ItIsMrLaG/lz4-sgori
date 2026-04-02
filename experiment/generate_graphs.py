#!/usr/bin/env python3
"""
Generate performance comparison graphs for LZ4 compression variations.
"""

import argparse
import json
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt
import numpy as np


@dataclass
class Metrics:
    """Container for calculated metrics."""

    compression_ratio: float
    compression_throughput: float  # MB/s
    compression_with_copy_throughput: float  # MB/s
    decompression_throughput: float  # MB/s
    decompression_with_copy_throughput: float  # MB/s
    total_throughput: float  # MB/s


class GraphGenerator:
    """Generate graphs comparing LZ4 compression variations."""

    COMPRESSION_TYPES = ["cont", "vect", "strm", "extd"]
    COMPRESSION_NAMES = {
        "cont": "Continuous",
        "vect": "Vectorized",
        "strm": "Streaming",
        "extd": "Extended",
    }

    def __init__(self, result_dir: Path, graph_dir: Path):
        self.result_dir = Path(result_dir)
        self.graph_dir = Path(graph_dir)
        self.graph_dir.mkdir(parents=True, exist_ok=True)

    def load_results(self) -> Dict[str, Dict[str, List[Metrics]]]:
        """
        Load all results and organize by test file and compression type.
        Returns: {test_file: {comp_type: [Metrics]}}
        """
        results = {}

        for comp_type in self.COMPRESSION_TYPES:
            comp_dir = self.result_dir / comp_type
            if not comp_dir.exists():
                print(f"Warning: Directory {comp_dir} not found")
                continue

            json_files = list(comp_dir.glob("*.json"))
            if not json_files:
                print(f"Warning: No JSON files found in {comp_dir}")
                continue

            for json_file in json_files:
                try:
                    with open(json_file) as f:
                        data = json.load(f)

                    test_file = data["test_file"]
                    if test_file not in results:
                        results[test_file] = {ct: [] for ct in self.COMPRESSION_TYPES}

                    # Calculate metrics
                    stats = data["statistics"]

                    # Use write statistics (for compression) and read statistics (for decompression)
                    decomp_size = stats.get("stats_w_decomp_size", 0)
                    comp_size = stats.get("stats_w_comp_size", 0)
                    comp_ns = stats.get("stats_w_comp_ns", 1)  # Avoid division by zero
                    decomp_ns = stats.get("stats_r_decomp_ns", 1)
                    total_ns = stats.get("stats_r_total_ns", 1)
                    copy_ns = stats.get("stats_r_copy_ns", 0) + stats.get("stats_w_copy_ns", 0)

                    if decomp_size == 0 or comp_size == 0:
                        print(f"Warning: Zero sizes in {json_file}, skipping...")
                        continue

                    # Calculate metrics
                    compression_ratio = decomp_size / comp_size if comp_size > 0 else 0

                    # Convert to MB/s (1 MB = 10^6 bytes, 1 ns = 10^-9 s)
                    compression_throughput = (
                        (decomp_size / 1e6) / (comp_ns / 1e9) if comp_ns > 0 else 0
                    )
                    compression_with_copy = (
                        (decomp_size / 1e6) / ((copy_ns + comp_ns) / 1e9)
                        if (copy_ns + comp_ns) > 0
                        else 0
                    )

                    decompression_throughput = (
                        (comp_size / 1e6) / (decomp_ns / 1e9) if decomp_ns > 0 else 0
                    )
                    decompression_with_copy = (
                        (comp_size / 1e6) / ((copy_ns + decomp_ns) / 1e9)
                        if (copy_ns + decomp_ns) > 0
                        else 0
                    )

                    total_throughput = (decomp_size / 1e6) / (total_ns / 1e9) if total_ns > 0 else 0

                    metrics = Metrics(
                        compression_ratio=compression_ratio,
                        compression_throughput=compression_throughput,
                        compression_with_copy_throughput=compression_with_copy,
                        decompression_throughput=decompression_throughput,
                        decompression_with_copy_throughput=decompression_with_copy,
                        total_throughput=total_throughput,
                    )

                    results[test_file][comp_type].append(metrics)

                except Exception as e:
                    print(f"Error processing {json_file}: {e}")
                    continue

        return results

    def calculate_stats(self, values: List[float]) -> Tuple[float, float]:
        """Calculate mean and standard deviation."""
        if not values:
            return 0, 0
        return statistics.mean(values), statistics.stdev(values) if len(values) > 1 else 0

    def plot_metric(
        self,
        results: Dict,
        metric_name: str,
        ylabel: str,
        title_prefix: str = "",
        with_copy: bool = False,
    ):
        """Generate a bar chart for a specific metric."""
        if not results:
            print("No results to plot")
            return

        test_files = list(results.keys())
        n_files = len(test_files)
        n_types = len(self.COMPRESSION_TYPES)

        # Setup plot
        _, ax = plt.subplots(figsize=(12, 6))
        x = np.arange(n_files)
        width = 0.8 / n_types

        colors = plt.cm.Set3(np.linspace(0, 1, n_types))

        for idx, comp_type in enumerate(self.COMPRESSION_TYPES):
            means = []
            stds = []

            for test_file in test_files:
                metrics_list = results[test_file][comp_type]
                if not metrics_list:
                    means.append(0)
                    stds.append(0)
                    continue

                if metric_name == "compression_ratio":
                    values = [m.compression_ratio for m in metrics_list]
                elif metric_name == "compression_throughput":
                    values = [m.compression_throughput for m in metrics_list]
                elif metric_name == "compression_with_copy":
                    values = [m.compression_with_copy_throughput for m in metrics_list]
                elif metric_name == "decompression_throughput":
                    values = [m.decompression_throughput for m in metrics_list]
                elif metric_name == "decompression_with_copy":
                    values = [m.decompression_with_copy_throughput for m in metrics_list]
                elif metric_name == "total_throughput":
                    values = [m.total_throughput for m in metrics_list]
                else:
                    values = []

                mean_val, std_val = self.calculate_stats(values)
                means.append(mean_val)
                stds.append(std_val)

            # Calculate position for bars
            pos = x + (idx - n_types / 2 + 0.5) * width
            _ = ax.bar(
                pos,
                means,
                width,
                label=self.COMPRESSION_NAMES[comp_type],
                color=colors[idx],
                yerr=stds,
                capsize=3,
                alpha=0.8,
            )

        # Customize plot
        ax.set_xlabel("Test Files", fontsize=12)
        ax.set_ylabel(ylabel, fontsize=12)

        title = f"{title_prefix} {metric_name.replace('_', ' ').title()}"
        if with_copy:
            title += " (with copy time)"
        ax.set_title(title, fontsize=14, fontweight="bold")

        ax.set_xticks(x)
        # Use just filename without path
        short_names = [Path(f).name for f in test_files]
        ax.set_xticklabels(short_names, rotation=45, ha="right")
        ax.legend(loc="upper right", bbox_to_anchor=(1.15, 1))

        ax.grid(True, alpha=0.3, axis="y")

        # Adjust layout
        plt.tight_layout()

        # Save figure
        filename = f"{metric_name}"
        if with_copy:
            filename += "_with_copy"
        output_path = self.graph_dir / f"{filename}.png"
        plt.savefig(output_path, dpi=300, bbox_inches="tight")
        plt.close()

        print(f"Generated graph: {output_path}")

    def generate_all_graphs(self):
        """Generate all comparison graphs."""
        print("Loading results...")
        results = self.load_results()

        if not results:
            print("No results found to generate graphs")
            return

        print(f"Found {len(results)} test files with results")
        for test_file, comp_results in results.items():
            total_measurements = sum(len(v) for v in comp_results.values())
            print(f"  - {Path(test_file).name}: {total_measurements} measurements")

        # Generate graphs for each metric
        graphs_config = [
            ("compression_ratio", "Compression Ratio", "Compression Efficiency"),
            (
                "compression_throughput",
                "Compression Throughput (MB/s)",
                "Compression Performance",
            ),
            (
                "decompression_throughput",
                "Decompression Throughput (MB/s)",
                "Decompression Performance",
            ),
            ("total_throughput", "Total Throughput (MB/s)", "Overall Performance"),
        ]

        for metric, ylabel, title_prefix in graphs_config:
            self.plot_metric(results, metric, ylabel, title_prefix)

        # Generate graphs with copy time included
        copy_graphs_config = [
            (
                "compression_throughput",
                "Compression Throughput (MB/s)",
                "Compression Performance",
                True,
            ),
            (
                "decompression_throughput",
                "Decompression Throughput (MB/s)",
                "Decompression Performance",
                True,
            ),
        ]

        for metric, ylabel, title_prefix, with_copy in copy_graphs_config:
            metric_with_copy = f"{metric.split('_')[0]}_with_copy"
            self.plot_metric(results, metric_with_copy, ylabel, title_prefix, with_copy)

        print(f"\nAll graphs saved to {self.graph_dir}")

        # Print summary statistics
        print("\n" + "=" * 60)
        print("SUMMARY STATISTICS")
        print("=" * 60)
        for test_file in results:
            print(f"\nTest File: {Path(test_file).name}")
            for comp_type in self.COMPRESSION_TYPES:
                metrics_list = results[test_file][comp_type]
                if metrics_list:
                    avg_ratio = statistics.mean([m.compression_ratio for m in metrics_list])
                    avg_comp = statistics.mean([m.compression_throughput for m in metrics_list])
                    avg_decomp = statistics.mean([m.decompression_throughput for m in metrics_list])
                    print(f"  {self.COMPRESSION_NAMES[comp_type]}:")
                    print(
                        f"    Ratio: {avg_ratio:.2f}x, Comp: {avg_comp:.2f} MB/s, Decomp: {avg_decomp:.2f} MB/s"
                    )


def main():
    parser = argparse.ArgumentParser(description="Generate LZ4 comparison graphs")
    parser.add_argument(
        "--result",
        default="./experiment/result",
        help="Path to intermediate results directory",
    )
    parser.add_argument("--graph", default="./experiment/graph", help="Path to graph directory")

    args = parser.parse_args()

    generator = GraphGenerator(args.result, args.graph)
    generator.generate_all_graphs()


if __name__ == "__main__":
    main()
