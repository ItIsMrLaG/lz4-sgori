#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Alexander Bugaev
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

"""
Generate performance comparison graphs for LZ4 compression variations.
"""

from argparse import ArgumentParser
from dataclasses import dataclass
from json import load
from pathlib import Path
from statistics import mean, stdev
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt
from numpy import arange

plt.switch_backend("svg")

plt.rcParams.update(
    {
        "font.size": 10,
        "axes.labelsize": 12,
        "axes.titlesize": 13,
        "axes.titleweight": "bold",
        "legend.fontsize": 9,
        "legend.frameon": False,
        "lines.linewidth": 1,
        "grid.linestyle": "--",
        "grid.alpha": 0.3,
    }
)


@dataclass
class IOMetrics:
    """Container for I/O statistics (separate for read and write)."""

    reqs_total: int = 0
    reqs_failed: int = 0
    min_vec: int = 0
    max_vec: int = 0
    vecs: int = 0
    segments: int = 0
    decomp_size: int = 0
    comp_size: int = 0
    mem_usage: int = 0
    copy_ns: int = 0
    comp_ns: int = 0
    decomp_ns: int = 0
    total_ns: int = 0


class GraphGenerator:
    COMPRESSION_TYPES = ["cont", "vect", "strm", "extd"]
    COMPRESSION_NAMES = {
        "cont": "Contiguous",
        "vect": "Vectorized",
        "strm": "Streaming",
        "extd": "Extended",
    }

    COLORS = {"cont": "#1f77b4", "vect": "#ff7f0e", "strm": "#2ca02c", "extd": "#d62728"}

    def __init__(self, result_dir: Path, graph_dir: Path):
        self.result_dir = Path(result_dir)
        self.graph_dir = Path(graph_dir)
        self.graph_dir.mkdir(parents=True, exist_ok=True)

    def load_results(self) -> Dict[str, Dict[str, List[Tuple[IOMetrics, IOMetrics]]]]:
        """
        Load all results organized by test file and compression type.
        Returns: {test_file: {comp_type: [(read_metrics, write_metrics)]}}
        """
        results: Dict[str, Dict[str, List[Tuple[IOMetrics, IOMetrics]]]] = {}

        for comp_type in self.COMPRESSION_TYPES:
            comp_dir = self.result_dir / comp_type
            if not comp_dir.exists():
                continue

            for json_file in comp_dir.glob("*.json"):
                try:
                    with open(json_file) as f:
                        data = load(f)

                    test_file = data["test_file"]
                    if test_file not in results:
                        results[test_file] = {ct: [] for ct in self.COMPRESSION_TYPES}

                    stats = data["statistics"]

                    # Read metrics
                    read_metrics = IOMetrics(
                        reqs_total=stats.get("stats_r_reqs_total", 0),
                        reqs_failed=stats.get("stats_r_reqs_failed", 0),
                        min_vec=stats.get("stats_r_min_vec", 0),
                        max_vec=stats.get("stats_r_max_vec", 0),
                        vecs=stats.get("stats_r_vecs", 0),
                        segments=stats.get("stats_r_segments", 0),
                        decomp_size=stats.get("stats_r_decomp_size", 0),
                        comp_size=stats.get("stats_r_comp_size", 0),
                        mem_usage=stats.get("stats_r_mem_usage", 0),
                        copy_ns=stats.get("stats_r_copy_ns", 0),
                        comp_ns=stats.get("stats_r_comp_ns", 0),
                        decomp_ns=stats.get("stats_r_decomp_ns", 0),
                        total_ns=stats.get("stats_r_total_ns", 0),
                    )

                    # Write metrics
                    write_metrics = IOMetrics(
                        reqs_total=stats.get("stats_w_reqs_total", 0),
                        reqs_failed=stats.get("stats_w_reqs_failed", 0),
                        min_vec=stats.get("stats_w_min_vec", 0),
                        max_vec=stats.get("stats_w_max_vec", 0),
                        vecs=stats.get("stats_w_vecs", 0),
                        segments=stats.get("stats_w_segments", 0),
                        decomp_size=stats.get("stats_w_decomp_size", 0),
                        comp_size=stats.get("stats_w_comp_size", 0),
                        mem_usage=stats.get("stats_w_mem_usage", 0),
                        copy_ns=stats.get("stats_w_copy_ns", 0),
                        comp_ns=stats.get("stats_w_comp_ns", 0),
                        decomp_ns=stats.get("stats_w_decomp_ns", 0),
                        total_ns=stats.get("stats_w_total_ns", 0),
                    )

                    results[test_file][comp_type].append((read_metrics, write_metrics))
                except Exception as e:
                    print(f"Error processing {json_file}: {e}")

        return results

    def calculate_stats(self, values: List[float]) -> Tuple[float, float]:
        if not values:
            return 0, 0
        return mean(values), stdev(values) if len(values) > 1 else 0

    def plot_compression_ratio(self, results: Dict) -> None:
        """
        Compression ratio = decomp_size / comp_size
        Uses overall metrics (sum of read and write)
        """
        test_files = list(results.keys())
        n_files = len(test_files)
        n_types = len(self.COMPRESSION_TYPES)

        _, ax = plt.subplots(figsize=(16, 7))
        x = arange(n_files)
        width = 0.8 / n_types

        for idx, comp_type in enumerate(self.COMPRESSION_TYPES):
            means, stds = [], []
            for test_file in test_files:
                metrics_list = results[test_file][comp_type]
                if not metrics_list:
                    means.append(0.0)
                    stds.append(0.0)
                    continue

                ratios = []
                for read_m, write_m in metrics_list:
                    total_decomp = read_m.decomp_size + write_m.decomp_size
                    total_comp = read_m.comp_size + write_m.comp_size
                    if total_comp > 0:
                        ratio = total_decomp / total_comp
                        ratios.append(ratio)

                mean_val, std_val = self.calculate_stats(ratios)
                means.append(mean_val)
                stds.append(std_val)

            pos = x + (idx - n_types / 2 + 0.5) * width
            bars = ax.bar(
                pos,
                means,
                width,
                label=self.COMPRESSION_NAMES[comp_type],
                color=self.COLORS[comp_type],
                yerr=stds,
                capsize=3,
                alpha=0.8,
                edgecolor="black",
                linewidth=0.8,
                error_kw={"elinewidth": 1, "capthick": 1},
            )

            max_height = max(means) if means else 1
            for bar, mean_val, std_val in zip(bars, means, stds):
                if mean_val > 0:
                    ax.text(
                        bar.get_x() + bar.get_width() / 2.0,
                        bar.get_height() + std_val + max_height * 0.02,
                        f"{mean_val:.1f}",
                        ha="center",
                        va="bottom",
                        fontsize=8,
                    )

        ax.set_xlabel("Test Files", fontsize=12, fontweight="bold")
        ax.set_ylabel("Compression Ratio", fontsize=12, fontweight="bold")
        ax.set_title(
            "Compression Ratio (Original Size / Compressed Size)",
            fontsize=13,
            fontweight="bold",
        )
        ax.set_xticks(x)
        ax.set_xticklabels([Path(f).name for f in test_files], rotation=45, ha="right")
        ax.legend(loc="center left", bbox_to_anchor=(1.02, 0.5))
        ax.grid(True, alpha=0.3, axis="y", linestyle="--")
        ax.set_axisbelow(True)

        plt.tight_layout()
        plt.savefig(self.graph_dir / "compression_ratio.svg", format="svg", bbox_inches="tight")
        plt.close()
        print("  Generated: compression_ratio.svg")

    def plot_memory_usage(self, results: Dict, operation: str) -> None:
        """
        Average memory usage per successful request = mem_usage / (reqs_total - reqs_failed)
        For READ: uses read metrics
        For WRITE: uses write metrics
        For OVERALL: uses combined metrics
        """
        test_files = list(results.keys())
        n_files = len(test_files)
        n_types = len(self.COMPRESSION_TYPES)

        _, ax = plt.subplots(figsize=(16, 7))
        x = arange(n_files)
        width = 0.8 / n_types

        for idx, comp_type in enumerate(self.COMPRESSION_TYPES):
            means, stds = [], []

            for test_file in test_files:
                metrics_list = results[test_file][comp_type]
                if not metrics_list:
                    means.append(0.0)
                    stds.append(0.0)
                    continue

                mem_per_req = []
                for read_m, write_m in metrics_list:
                    if operation == "read":
                        successful_reqs = read_m.reqs_total - read_m.reqs_failed
                        if successful_reqs > 0 and read_m.mem_usage > 0:
                            mem = read_m.mem_usage / successful_reqs
                            mem_per_req.append(mem / 1024)  # Convert to KB
                    elif operation == "write":
                        successful_reqs = write_m.reqs_total - write_m.reqs_failed
                        if successful_reqs > 0 and write_m.mem_usage > 0:
                            mem = write_m.mem_usage / successful_reqs
                            mem_per_req.append(mem / 1024)  # Convert to KB
                    else:  # overall
                        total_mem = read_m.mem_usage + write_m.mem_usage
                        total_reqs = read_m.reqs_total + write_m.reqs_total
                        total_failed = read_m.reqs_failed + write_m.reqs_failed
                        successful_reqs = total_reqs - total_failed
                        if successful_reqs > 0 and total_mem > 0:
                            mem = total_mem / successful_reqs
                            mem_per_req.append(mem / 1024)  # Convert to KB

                mean_val, std_val = self.calculate_stats(mem_per_req)
                means.append(mean_val)
                stds.append(std_val)

            pos = x + (idx - n_types / 2 + 0.5) * width
            bars = ax.bar(
                pos,
                means,
                width,
                label=self.COMPRESSION_NAMES[comp_type],
                color=self.COLORS[comp_type],
                yerr=stds,
                capsize=3,
                alpha=0.75,
                edgecolor="black",
                linewidth=0.8,
                error_kw={"elinewidth": 1, "capthick": 1},
            )

            max_height = max(means) if means else 1
            for bar, mean_val, std_val in zip(bars, means, stds):
                if mean_val > 0:
                    ax.text(
                        bar.get_x() + bar.get_width() / 2.0,
                        bar.get_height() + std_val + max_height * 0.02,
                        f"{mean_val:.1f}",
                        ha="center",
                        va="bottom",
                        fontsize=7,
                    )

        title_map = {
            "read": "Memory Usage (Read Operation)",
            "write": "Memory Usage (Write Operation)",
            "overall": "Memory Usage (Read + Write)",
        }

        ax.set_xlabel("Test Files", fontsize=12, fontweight="bold")
        ax.set_ylabel("Average Memory Usage per Request (KB)", fontsize=12, fontweight="bold")
        ax.set_title(title_map[operation], fontsize=12, fontweight="bold")
        ax.set_xticks(x)
        ax.set_xticklabels([Path(f).name for f in test_files], rotation=45, ha="right")
        ax.legend(loc="center left", bbox_to_anchor=(1.02, 0.5))
        ax.grid(True, alpha=0.3, axis="y", linestyle="--")
        ax.set_axisbelow(True)

        plt.tight_layout()
        output_path = self.graph_dir / f"memory_usage_{operation}.svg"
        plt.savefig(output_path, format="svg", bbox_inches="tight")
        plt.close()
        print(f"  Generated: memory_usage_{operation}.svg")

    def plot_compression_throughput(self, results: Dict, operation: str) -> None:
        """
        Compression throughput = decomp_size / comp_ns
        For both READ and WRITE operations (device compresses on every I/O)
        """
        test_files = list(results.keys())
        n_files = len(test_files)
        n_types = len(self.COMPRESSION_TYPES)

        _, ax = plt.subplots(figsize=(16, 7))
        x = arange(n_files)
        width = 0.8 / n_types

        for idx, comp_type in enumerate(self.COMPRESSION_TYPES):
            base_throughputs = []  # Actual throughput (with copy time)
            copy_overheads = []  # Throughput lost to copy time
            base_stds = []

            for test_file in test_files:
                metrics_list = results[test_file][comp_type]
                if not metrics_list:
                    base_throughputs.append(0.0)
                    copy_overheads.append(0.0)
                    base_stds.append(0.0)
                    continue

                if operation == "read":
                    # Use read metrics for compression during read
                    values_actual = []
                    values_ideal = []

                    for read_m, _ in metrics_list:
                        if read_m.comp_ns > 0 and read_m.decomp_size > 0:
                            # Actual throughput (includes copy time)
                            total_time = read_m.comp_ns + read_m.copy_ns
                            tp_actual = (
                                (read_m.decomp_size / 1e6) / (total_time / 1e9)
                                if total_time > 0
                                else 0
                            )
                            values_actual.append(tp_actual)

                            # Ideal throughput (no copy time)
                            tp_ideal = (read_m.decomp_size / 1e6) / (read_m.comp_ns / 1e9)
                            values_ideal.append(tp_ideal)

                    mean_actual, std_actual = self.calculate_stats(values_actual)
                    mean_ideal, _ = self.calculate_stats(values_ideal)

                    overhead = mean_ideal - mean_actual if mean_ideal > mean_actual else 0

                    base_throughputs.append(mean_actual)
                    copy_overheads.append(overhead)
                    base_stds.append(std_actual)

                elif operation == "write":
                    # Use write metrics for compression during write
                    values_actual = []
                    values_ideal = []

                    for _, write_m in metrics_list:
                        if write_m.comp_ns > 0 and write_m.decomp_size > 0:
                            total_time = write_m.comp_ns + write_m.copy_ns
                            tp_actual = (
                                (write_m.decomp_size / 1e6) / (total_time / 1e9)
                                if total_time > 0
                                else 0
                            )
                            values_actual.append(tp_actual)

                            tp_ideal = (write_m.decomp_size / 1e6) / (write_m.comp_ns / 1e9)
                            values_ideal.append(tp_ideal)

                    mean_actual, std_actual = self.calculate_stats(values_actual)
                    mean_ideal, _ = self.calculate_stats(values_ideal)

                    overhead = mean_ideal - mean_actual if mean_ideal > mean_actual else 0

                    base_throughputs.append(mean_actual)
                    copy_overheads.append(overhead)
                    base_stds.append(std_actual)

                elif operation == "overall":
                    # Combine read and write metrics for overall compression
                    per_run_actual = []
                    per_run_ideal = []

                    for read_m, write_m in metrics_list:
                        run_decomp = read_m.decomp_size + write_m.decomp_size
                        run_comp_ns = read_m.comp_ns + write_m.comp_ns
                        run_copy_ns = read_m.copy_ns + write_m.copy_ns

                        if run_comp_ns > 0:
                            # Actual throughput for this run (includes copy time)
                            total_time = run_comp_ns + run_copy_ns
                            tp_actual = (
                                (run_decomp / 1e6) / (total_time / 1e9) if total_time > 0 else 0
                            )
                            per_run_actual.append(tp_actual)

                            # Ideal throughput for this run (no copy time)
                            tp_ideal = (run_decomp / 1e6) / (run_comp_ns / 1e9)
                            per_run_ideal.append(tp_ideal)

                    mean_actual, std_actual = self.calculate_stats(per_run_actual)
                    mean_ideal, _ = self.calculate_stats(per_run_ideal)

                    overhead = mean_ideal - mean_actual if mean_ideal > mean_actual else 0

                    base_throughputs.append(mean_actual)
                    copy_overheads.append(overhead)
                    base_stds.append(std_actual)

            pos = x + (idx - n_types / 2 + 0.5) * width
            has_overhead = any(o > 0 for o in copy_overheads)

            if has_overhead:
                _ = ax.bar(
                    pos,
                    base_throughputs,
                    width,
                    label=f"{self.COMPRESSION_NAMES[comp_type]} (actual)",
                    color=self.COLORS[comp_type],
                    yerr=base_stds,
                    capsize=3,
                    alpha=0.75,
                    edgecolor="black",
                    linewidth=0.8,
                    error_kw={"elinewidth": 1, "capthick": 1},
                )

                _ = ax.bar(
                    pos,
                    copy_overheads,
                    width,
                    bottom=base_throughputs,
                    label=f"{self.COMPRESSION_NAMES[comp_type]} (copy loss)",
                    color=self.COLORS[comp_type],
                    alpha=0.3,
                    edgecolor="none",
                    linewidth=0,
                )

                # Label only the actual throughput value (bottom bar)
                max_height = max(base_throughputs) if base_throughputs else 1
                for i, (base, std) in enumerate(zip(base_throughputs, base_stds)):
                    if base > 0:
                        label_y = base + std + (max_height * 0.02)
                        ax.text(
                            pos[i], label_y, f"{base:.1f}", ha="center", va="bottom", fontsize=7
                        )
            else:
                bars = ax.bar(
                    pos,
                    base_throughputs,
                    width,
                    label=self.COMPRESSION_NAMES[comp_type],
                    color=self.COLORS[comp_type],
                    yerr=base_stds,
                    capsize=3,
                    alpha=0.75,
                    edgecolor="black",
                    linewidth=0.8,
                    error_kw={"elinewidth": 1, "capthick": 1},
                )

                max_height = max(base_throughputs) if base_throughputs else 1
                for bar, val, std in zip(bars, base_throughputs, base_stds):
                    if val > 0:
                        ax.text(
                            bar.get_x() + bar.get_width() / 2.0,
                            bar.get_height() + std + max_height * 0.02,
                            f"{val:.1f}",
                            ha="center",
                            va="bottom",
                            fontsize=7,
                        )

        title_map = {
            "read": "Compression Throughput (Read Operation)",
            "write": "Compression Throughput (Write Operation)",
            "overall": "Overall Compression Throughput (Read + Write)",
        }

        ax.set_xlabel("Test Files", fontsize=12, fontweight="bold")
        ax.set_ylabel("Compression Throughput (MB/s)", fontsize=12, fontweight="bold")
        ax.set_title(title_map[operation], fontsize=12, fontweight="bold")
        ax.set_xticks(x)
        ax.set_xticklabels([Path(f).name for f in test_files], rotation=45, ha="right")

        handles, labels = ax.get_legend_handles_labels()
        unique = dict(zip(labels, handles))
        ax.legend(unique.values(), unique.keys(), loc="center left", bbox_to_anchor=(1.02, 0.5))

        ax.grid(True, alpha=0.3, axis="y", linestyle="--")
        ax.set_axisbelow(True)

        plt.tight_layout()
        output_path = self.graph_dir / f"compression_throughput_{operation}.svg"
        plt.savefig(output_path, format="svg", bbox_inches="tight")
        plt.close()
        print(f"  Generated: compression_throughput_{operation}.svg")

    def plot_decompression_throughput(self, results: Dict, operation: str) -> None:
        """
        Decompression throughput = comp_size / decomp_ns
        For both READ and WRITE operations (device decompresses on every I/O)
        """
        test_files = list(results.keys())
        n_files = len(test_files)
        n_types = len(self.COMPRESSION_TYPES)

        _, ax = plt.subplots(figsize=(16, 7))
        x = arange(n_files)
        width = 0.8 / n_types

        for idx, comp_type in enumerate(self.COMPRESSION_TYPES):
            base_throughputs = []  # Actual throughput (with copy time)
            copy_overheads = []  # Throughput lost to copy time
            base_stds = []

            for test_file in test_files:
                metrics_list = results[test_file][comp_type]
                if not metrics_list:
                    base_throughputs.append(0.0)
                    copy_overheads.append(0.0)
                    base_stds.append(0.0)
                    continue

                if operation == "read":
                    # Use read metrics for decompression during read
                    values_actual = []
                    values_ideal = []

                    for read_m, _ in metrics_list:
                        if read_m.decomp_ns > 0 and read_m.comp_size > 0:
                            total_time = read_m.decomp_ns + read_m.copy_ns
                            tp_actual = (
                                (read_m.comp_size / 1e6) / (total_time / 1e9)
                                if total_time > 0
                                else 0
                            )
                            values_actual.append(tp_actual)

                            tp_ideal = (read_m.comp_size / 1e6) / (read_m.decomp_ns / 1e9)
                            values_ideal.append(tp_ideal)

                    mean_actual, std_actual = self.calculate_stats(values_actual)
                    mean_ideal, _ = self.calculate_stats(values_ideal)

                    overhead = mean_ideal - mean_actual if mean_ideal > mean_actual else 0

                    base_throughputs.append(mean_actual)
                    copy_overheads.append(overhead)
                    base_stds.append(std_actual)

                elif operation == "write":
                    # Use write metrics for decompression during write
                    values_actual = []
                    values_ideal = []

                    for _, write_m in metrics_list:
                        if write_m.decomp_ns > 0 and write_m.comp_size > 0:
                            total_time = write_m.decomp_ns + write_m.copy_ns
                            tp_actual = (
                                (write_m.comp_size / 1e6) / (total_time / 1e9)
                                if total_time > 0
                                else 0
                            )
                            values_actual.append(tp_actual)

                            tp_ideal = (write_m.comp_size / 1e6) / (write_m.decomp_ns / 1e9)
                            values_ideal.append(tp_ideal)

                    mean_actual, std_actual = self.calculate_stats(values_actual)
                    mean_ideal, _ = self.calculate_stats(values_ideal)

                    overhead = mean_ideal - mean_actual if mean_ideal > mean_actual else 0

                    base_throughputs.append(mean_actual)
                    copy_overheads.append(overhead)
                    base_stds.append(std_actual)

                elif operation == "overall":
                    # Combine read and write metrics for overall decompression
                    per_run_actual = []
                    per_run_ideal = []

                    for read_m, write_m in metrics_list:
                        run_comp = read_m.comp_size + write_m.comp_size
                        run_decomp_ns = read_m.decomp_ns + write_m.decomp_ns
                        run_copy_ns = read_m.copy_ns + write_m.copy_ns

                        if run_decomp_ns > 0:
                            # Actual throughput for this run (includes copy time)
                            total_time = run_decomp_ns + run_copy_ns
                            tp_actual = (
                                (run_comp / 1e6) / (total_time / 1e9) if total_time > 0 else 0
                            )
                            per_run_actual.append(tp_actual)

                            # Ideal throughput for this run (no copy time)
                            tp_ideal = (run_comp / 1e6) / (run_decomp_ns / 1e9)
                            per_run_ideal.append(tp_ideal)

                    mean_actual, std_actual = self.calculate_stats(per_run_actual)
                    mean_ideal, _ = self.calculate_stats(per_run_ideal)

                    overhead = mean_ideal - mean_actual if mean_ideal > mean_actual else 0

                    base_throughputs.append(mean_actual)
                    copy_overheads.append(overhead)
                    base_stds.append(std_actual)

            pos = x + (idx - n_types / 2 + 0.5) * width
            has_overhead = any(o > 0 for o in copy_overheads)

            if has_overhead:
                _ = ax.bar(
                    pos,
                    base_throughputs,
                    width,
                    label=f"{self.COMPRESSION_NAMES[comp_type]} (actual)",
                    color=self.COLORS[comp_type],
                    yerr=base_stds,
                    capsize=3,
                    alpha=0.75,
                    edgecolor="black",
                    linewidth=0.8,
                    error_kw={"elinewidth": 1, "capthick": 1},
                )

                _ = ax.bar(
                    pos,
                    copy_overheads,
                    width,
                    bottom=base_throughputs,
                    label=f"{self.COMPRESSION_NAMES[comp_type]} (copy loss)",
                    color=self.COLORS[comp_type],
                    alpha=0.3,
                    edgecolor="none",
                    linewidth=0,
                )

                # Label only the actual throughput value (bottom bar)
                max_height = max(base_throughputs) if base_throughputs else 1
                for i, (base, std) in enumerate(zip(base_throughputs, base_stds)):
                    if base > 0:
                        label_y = base + std + (max_height * 0.02)
                        ax.text(
                            pos[i], label_y, f"{base:.1f}", ha="center", va="bottom", fontsize=7
                        )
            else:
                bars = ax.bar(
                    pos,
                    base_throughputs,
                    width,
                    label=self.COMPRESSION_NAMES[comp_type],
                    color=self.COLORS[comp_type],
                    yerr=base_stds,
                    capsize=3,
                    alpha=0.75,
                    edgecolor="black",
                    linewidth=0.8,
                    error_kw={"elinewidth": 1, "capthick": 1},
                )

                max_height = max(base_throughputs) if base_throughputs else 1
                for bar, val, std in zip(bars, base_throughputs, base_stds):
                    if val > 0:
                        ax.text(
                            bar.get_x() + bar.get_width() / 2.0,
                            bar.get_height() + std + max_height * 0.02,
                            f"{val:.1f}",
                            ha="center",
                            va="bottom",
                            fontsize=7,
                        )

        title_map = {
            "read": "Decompression Throughput (Read Operation)",
            "write": "Decompression Throughput (Write Operation)",
            "overall": "Overall Decompression Throughput (Read + Write)",
        }

        ax.set_xlabel("Test Files", fontsize=12, fontweight="bold")
        ax.set_ylabel("Decompression Throughput (MB/s)", fontsize=12, fontweight="bold")
        ax.set_title(title_map[operation], fontsize=12, fontweight="bold")
        ax.set_xticks(x)
        ax.set_xticklabels([Path(f).name for f in test_files], rotation=45, ha="right")

        handles, labels = ax.get_legend_handles_labels()
        unique = dict(zip(labels, handles))
        ax.legend(unique.values(), unique.keys(), loc="center left", bbox_to_anchor=(1.02, 0.5))

        ax.grid(True, alpha=0.3, axis="y", linestyle="--")
        ax.set_axisbelow(True)

        plt.tight_layout()
        output_path = self.graph_dir / f"decompression_throughput_{operation}.svg"
        plt.savefig(output_path, format="svg", bbox_inches="tight")
        plt.close()
        print(f"  Generated: decompression_throughput_{operation}.svg")

    def plot_total_throughput(self, results: Dict, operation: str) -> None:
        """
        Total throughput = decomp_size / total_ns
        For READ: uses stats_r_total_ns and stats_r_decomp_size
        For WRITE: uses stats_w_total_ns and stats_w_decomp_size
        For OVERALL: combines both
        """
        test_files = list(results.keys())
        n_files = len(test_files)
        n_types = len(self.COMPRESSION_TYPES)

        _, ax = plt.subplots(figsize=(16, 7))
        x = arange(n_files)
        width = 0.8 / n_types

        for idx, comp_type in enumerate(self.COMPRESSION_TYPES):
            means = []
            stds = []

            for test_file in test_files:
                metrics_list = results[test_file][comp_type]
                if not metrics_list:
                    means.append(0.0)
                    stds.append(0.0)
                    continue

                if operation == "read":
                    # Total throughput for read operations
                    values = []
                    for read_m, _ in metrics_list:
                        if read_m.total_ns > 0 and read_m.decomp_size > 0:
                            tp = (read_m.decomp_size / 1e6) / (read_m.total_ns / 1e9)
                            values.append(tp)
                    mean_val, std_val = self.calculate_stats(values)
                    means.append(mean_val)
                    stds.append(std_val)

                elif operation == "write":
                    # Total throughput for write operations
                    values = []
                    for _, write_m in metrics_list:
                        if write_m.total_ns > 0 and write_m.decomp_size > 0:
                            tp = (write_m.decomp_size / 1e6) / (write_m.total_ns / 1e9)
                            values.append(tp)
                    mean_val, std_val = self.calculate_stats(values)
                    means.append(mean_val)
                    stds.append(std_val)

                elif operation == "overall":
                    # Overall total throughput (combine read and write)
                    total_decomp = 0
                    total_time = 0
                    per_run_tp = []

                    for read_m, write_m in metrics_list:
                        run_decomp = read_m.decomp_size + write_m.decomp_size
                        run_time = read_m.total_ns + write_m.total_ns

                        if run_time > 0:
                            run_tp = (run_decomp / 1e6) / (run_time / 1e9)
                            per_run_tp.append(run_tp)

                            total_decomp += run_decomp
                            total_time += run_time

                    if total_time > 0:
                        overall_tp = (total_decomp / 1e6) / (total_time / 1e9)
                        _, std_val = self.calculate_stats(per_run_tp)
                        means.append(overall_tp)
                        stds.append(std_val)
                    else:
                        means.append(0)
                        stds.append(0)

            pos = x + (idx - n_types / 2 + 0.5) * width
            bars = ax.bar(
                pos,
                means,
                width,
                label=self.COMPRESSION_NAMES[comp_type],
                color=self.COLORS[comp_type],
                yerr=stds,
                capsize=3,
                alpha=0.75,
                edgecolor="black",
                linewidth=0.8,
                error_kw={"elinewidth": 1, "capthick": 1},
            )

            max_height = max(means) if means else 1
            for bar, mean_val, std_val in zip(bars, means, stds):
                if mean_val > 0:
                    ax.text(
                        bar.get_x() + bar.get_width() / 2.0,
                        bar.get_height() + std_val + max_height * 0.02,
                        f"{mean_val:.1f}",
                        ha="center",
                        va="bottom",
                        fontsize=7,
                    )

        title_map = {
            "read": "Total Throughput (Read Operation)",
            "write": "Total Throughput (Write Operation)",
            "overall": "Overall Total Throughput (Read + Write)",
        }

        subtitle = "\n(Includes compression, decompression, copy, and device I/O)"

        ax.set_xlabel("Test Files", fontsize=12, fontweight="bold")
        ax.set_ylabel("Total Throughput (MB/s)", fontsize=12, fontweight="bold")
        ax.set_title(title_map[operation] + subtitle, fontsize=11, fontweight="bold")
        ax.set_xticks(x)
        ax.set_xticklabels([Path(f).name for f in test_files], rotation=45, ha="right")
        ax.legend(loc="center left", bbox_to_anchor=(1.02, 0.5))
        ax.grid(True, alpha=0.3, axis="y", linestyle="--")
        ax.set_axisbelow(True)

        plt.tight_layout()
        output_path = self.graph_dir / f"total_throughput_{operation}.svg"
        plt.savefig(output_path, format="svg", bbox_inches="tight")
        plt.close()
        print(f"  Generated: total_throughput_{operation}.svg")

    def generate_all_graphs(self) -> None:
        """Generate all graphs."""
        print("Loading results...")
        results = self.load_results()

        if not results:
            print("No results found")
            return

        print(f"Found {len(results)} test files with results\n")

        print("Generating graphs:")

        # 1. Compression ratio (overall only)
        print("  1/13: Compression ratio (overall)")
        self.plot_compression_ratio(results)

        # 2-4. Memory usage (for read, write, and overall)
        print("  2/13: Memory usage (read)")
        self.plot_memory_usage(results, "read")

        print("  3/13: Memory usage (write)")
        self.plot_memory_usage(results, "write")

        print("  4/13: Memory usage (overall)")
        self.plot_memory_usage(results, "overall")

        # 5-7. Compression throughput (for read, write, and overall)
        print("  5/13: Compression throughput (read)")
        self.plot_compression_throughput(results, "read")

        print("  6/13: Compression throughput (write)")
        self.plot_compression_throughput(results, "write")

        print("  7/13: Compression throughput (overall)")
        self.plot_compression_throughput(results, "overall")

        # 8-10. Decompression throughput (for read, write, and overall)
        print("  8/13: Decompression throughput (read)")
        self.plot_decompression_throughput(results, "read")

        print("  9/13: Decompression throughput (write)")
        self.plot_decompression_throughput(results, "write")

        print(" 10/13: Decompression throughput (overall)")
        self.plot_decompression_throughput(results, "overall")

        # 11-13. Total throughput (for read, write, and overall)
        print(" 11/13: Total throughput (read)")
        self.plot_total_throughput(results, "read")

        print(" 12/13: Total throughput (write)")
        self.plot_total_throughput(results, "write")

        print(" 13/13: Total throughput (overall)")
        self.plot_total_throughput(results, "overall")

        print(f"\nAll graphs saved to: {self.graph_dir}")


def main() -> None:
    parser = ArgumentParser(description="Generate LZ4 comparison graphs")
    parser.add_argument(
        "--result", default="./result", help="Path to intermediate results directory"
    )
    parser.add_argument("--graph", default="./graph", help="Path to graph directory")

    args = parser.parse_args()

    generator = GraphGenerator(Path(args.result), Path(args.graph))
    generator.generate_all_graphs()


if __name__ == "__main__":
    main()
