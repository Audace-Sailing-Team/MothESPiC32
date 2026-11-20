"""Reproducible AJ-SR04M characterization; also tangled from characterization.org.

Run from the project directory: python analysis.py
Requires numpy, pandas, scipy, matplotlib and scikit-learn.
"""
# --- 1. Imports and experiment registry ---
from pathlib import Path
import re
import hashlib

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.stats import norm
from sklearn.mixture import GaussianMixture
from threadpoolctl import threadpool_limits

BASE = Path(__file__).resolve().parent if "__file__" in globals() else Path.cwd()
DATA = BASE / "data"
FIG = BASE / "figures"
RESULTS = BASE / "results"
FIG.mkdir(parents=True, exist_ok=True)
RESULTS.mkdir(parents=True, exist_ok=True)

# Nominal reference height in mm. The 108 and 128 mm runs include movement.
RUNS = [
    ("69.txt", 69, ""),
    ("108 abbassat.txt", 108, "lowered; height may vary"),
    ("128 abbassato da 17 un po scosso.txt", 128,
     "lowered from an earlier setting; shaken; dynamic"),
    ("131.txt", 131, ""),
    ("170.txt", 170, ""),
    ("188.txt", 188, ""),
    ("201.txt", 201, ""),
    ("283.txt", 283, ""),
]
MOVING_RUNS = ("108 abbassat.txt", "128 abbassato da 17 un po scosso.txt")
STABLE_RUNS = tuple(f for f, _, _ in RUNS if f not in MOVING_RUNS)
LOG_CONVERSION_MM_PER_US = 0.1715   # observed firmware conversion, 343 m/s
FIT_DISTANCE_CEILING_MM = 450        # only for fits and histogram axes


# --- 2. Serial log parser and validation ---
RECORD = re.compile(
    rb"I\s*\(\s*(?P<time_ms>\d+)\s*\)\s*ULTRASONIC:\s*"
    rb"ECHO:\s*(?P<echo_us>\d+)\s*us\s*\|\s*"
    rb"DISTANZA:\s*(?P<distance_mm>\d+)\s*mm\s*\|\s*"
    rb"STATUS:\s*(?P<status>\d+)"
)


def parse_run(filename, nominal_mm, note):
    raw = (DATA / filename).read_bytes()
    matches = list(RECORD.finditer(raw))
    if not matches:
        raise ValueError(f"No complete measurements parsed in {filename!r}")
    df = pd.DataFrame.from_records(
        [{k: int(v) for k, v in m.groupdict().items()} for m in matches]
    )
    df.insert(0, "run", filename)
    df.insert(1, "nominal_mm", nominal_mm)
    df["note"] = note
    df["sample_index"] = np.arange(len(df))
    # ESP-IDF logging timestamps are milliseconds, although serial output
    # can be delayed. Acquisition timestamps do not imply a uniform sample rate.
    df["elapsed_s"] = (df.time_ms - df.time_ms.iloc[0]) / 1000.0
    df["is_valid"] = (
        (df.status == 3) & (df.echo_us > 0) & (df.distance_mm > 0)
    )
    df.attrs["sha256"] = hashlib.sha256(raw).hexdigest()
    df.attrs["complete_records"] = len(matches)
    df.attrs["echo_tags"] = raw.count(b"ULTRASONIC: ECHO:")
    df.attrs["damaged_or_incomplete_echo_tags"] = (
        df.attrs["echo_tags"] - len(matches)
    )

    valid = df.loc[df.is_valid]
    reconstructed = np.floor(
        LOG_CONVERSION_MM_PER_US * valid.echo_us.to_numpy()
    ).astype(int)
    if not np.array_equal(reconstructed, valid.distance_mm.to_numpy()):
        raise AssertionError(f"Inconsistent pulse/distance conversion in {filename}")
    if not df.time_ms.is_monotonic_increasing:
        raise AssertionError(f"Nonmonotonic timestamps in {filename}")
    return df


datasets = {f: parse_run(f, h, note) for f, h, note in RUNS}
all_events = pd.concat(datasets.values(), ignore_index=True)
all_events.to_csv(RESULTS / "parsed_measurements.csv", index=False)


# --- 3. Per-recording descriptive statistics ---
def summarize(df):
    valid = df.loc[df.is_valid, "distance_mm"].astype(float)
    echo = df.loc[df.is_valid, "echo_us"].astype(float)
    n = len(df)
    if not len(valid):
        raise ValueError(f"No valid observations in {df.run.iloc[0]}")
    return {
        "run": str(df.run.iloc[0]),
        "nominal_mm": int(df.nominal_mm.iloc[0]),
        "n_parsed": n,
        "n_valid": len(valid),
        "n_no_echo": int((df.status == 0).sum()),
        "n_stuck": int((df.status == 1).sum()),
        "n_other_status": int((~df.status.isin([0, 1, 3])).sum()),
        "valid_fraction": len(valid) / n,
        "mean_mm": valid.mean(),
        "sd_mm": valid.std(ddof=1),
        "median_mm": valid.median(),
        "q05_mm": valid.quantile(.05),
        "q95_mm": valid.quantile(.95),
        "min_mm": valid.min(),
        "max_mm": valid.max(),
        "mode_mm": float(valid.mode().iloc[0]),
        "bias_mm": valid.mean() - df.nominal_mm.iloc[0],
        "rmse_mm": np.sqrt(np.mean((valid - df.nominal_mm.iloc[0])**2)),
        "mean_echo_us": echo.mean(),
        "sd_echo_us": echo.std(ddof=1),
        "n_over_450_mm": int((valid > FIT_DISTANCE_CEILING_MM).sum()),
    }


summary = pd.DataFrame([summarize(datasets[f]) for f, _, _ in RUNS])
summary.to_csv(RESULTS / "per_run_summary.csv", index=False)

# Preserve exploratory 15-record windows for inspection, without claiming
# correspondence to firmware reporting batches.
window_rows = []
for filename, df in datasets.items():
    for index, block in df.groupby(df.sample_index // 15, sort=True):
        v = block.loc[block.is_valid, "distance_mm"].astype(float)
        window_rows.append({
            "run": filename,
            "nominal_mm": int(block.nominal_mm.iloc[0]),
            "window_index": int(index),
            "elapsed_mid_s": block.elapsed_s.median(),
            "n_parsed": len(block),
            "n_valid": len(v),
            "median_mm": v.median(),
            "q05_mm": v.quantile(.05),
            "q95_mm": v.quantile(.95),
        })
windows = pd.DataFrame(window_rows)
windows.to_csv(RESULTS / "15_event_windows.csv", index=False)

# Retain the report's exploratory minimum-distance baseline.
BASELINE_NAMES = ("69.txt", "170.txt", "188.txt", "201.txt", "283.txt")
base = summary.set_index("run").loc[list(BASELINE_NAMES)]
baseline_offset_mm = float(np.median(
    base.mode_mm.to_numpy() - np.maximum(base.nominal_mm.to_numpy(), 200)
))


def baseline_distance(h_mm):
    return np.maximum(np.asarray(h_mm, dtype=float), 200.0) + baseline_offset_mm


# --- 4. Conditional models, uncertainty on medians, and component analysis ---
# All valid events are retained in the descriptive statistics. The 450 mm
# ceiling applies only to statistical fits and to selected figure axes.
# An ECHO duration is recorded in integer microseconds. For histogram overlays,
# a continuous fitted density is integrated over each recorded 1-us interval.
fit_samples = {}
fit_rows = []
for filename, nominal_mm, _ in RUNS:
    valid = datasets[filename].loc[datasets[filename].is_valid]
    eligible = valid.loc[valid.distance_mm <= FIT_DISTANCE_CEILING_MM]
    x = eligible.echo_us.to_numpy(dtype=float)
    if len(x) < 2:
        raise ValueError(f"Insufficient fitting data in {filename}")
    fit_samples[filename] = x
    mu, sigma = norm.fit(x)
    fit_rows.append({
        "run": filename, "nominal_mm": nominal_mm,
        "n_valid": len(valid), "n_fit": len(x),
        "n_excluded_over_450_mm": len(valid) - len(x),
        "gaussian_mean_echo_us": mu, "gaussian_sd_echo_us": sigma,
        "gaussian_mean_distance_mm": LOG_CONVERSION_MM_PER_US * mu,
        "gaussian_sd_distance_mm": LOG_CONVERSION_MM_PER_US * sigma,
    })
single_gaussian = pd.DataFrame(fit_rows)
single_gaussian.to_csv(RESULTS / "gaussian_fit_summary.csv", index=False)

# Two well-separated groups are visible in the nominally stationary 131 and
# 188 mm runs. Compare a single Gaussian with two fitted components for those
# runs. Other stationary recordings retain a one-Gaussian diagnostic fit;
# further splitting small integer-time irregularities is not the objective.
# For moving runs, explore K=1...6 as an exploratory bound, not as a
# determination of the physical number of reflected echoes.
SECONDARY_RUNS = ("131.txt", "188.txt")
MULTIMODAL_RUNS = (*MOVING_RUNS, *SECONDARY_RUNS)
mixture_models = {}
bic_rows = []
component_rows = []
with threadpool_limits(limits=1):
    for filename, nominal_mm, _ in RUNS:
        x = fit_samples[filename].reshape(-1, 1)
        if filename in MOVING_RUNS:
            candidate_counts = range(1, 7)
        elif filename in SECONDARY_RUNS:
            candidate_counts = (1, 2)
        else:
            candidate_counts = (1,)
        candidates = []
        for k in candidate_counts:
            model = GaussianMixture(
                n_components=k, n_init=5, max_iter=400,
                reg_covar=1.0, random_state=2026,
            ).fit(x)
            if not model.converged_:
                raise RuntimeError(f"GMM did not converge: {filename}, K={k}")
            bic = float(model.bic(x))
            bic_rows.append({"run": filename, "nominal_mm": nominal_mm,
                             "k": k, "bic": bic})
            candidates.append((bic, model))
        chosen_bic, chosen = min(candidates, key=lambda pair: pair[0])
        mixture_models[filename] = chosen
        for component_index, j in enumerate(np.argsort(chosen.means_.ravel()), 1):
            mu = float(chosen.means_[j, 0])
            sd = float(np.sqrt(chosen.covariances_[j, 0, 0]))
            component_rows.append({
                "run": filename, "nominal_mm": nominal_mm,
                "selected_k": chosen.n_components, "component": component_index,
                "weight": float(chosen.weights_[j]),
                "mean_echo_us": mu, "sd_echo_us": sd,
                "mean_distance_mm": LOG_CONVERSION_MM_PER_US * mu,
                "sd_distance_mm": LOG_CONVERSION_MM_PER_US * sd,
                "selected_bic": chosen_bic,
            })
components = pd.DataFrame(component_rows)
bic_table = pd.DataFrame(bic_rows)
bic_table.to_csv(RESULTS / "mixture_bic.csv", index=False)
components.to_csv(RESULTS / "mixture_components.csv", index=False)

# The law of total variance supplies a numerical application of the mixture
# model: within-component scatter + between-component scatter. Values are
# conditional on a successful event within the fitting range.
variance_rows = []
for filename, h0, _ in RUNS:
    group = components.loc[components.run == filename]
    weight = group.weight.to_numpy()
    mu = group.mean_echo_us.to_numpy()
    sd = group.sd_echo_us.to_numpy()
    mu_all = np.dot(weight, mu)
    within = float(np.dot(weight, sd**2))
    between = float(np.dot(weight, (mu - mu_all)**2))
    total = within + between
    variance_rows.append({
        "run": filename, "nominal_mm": h0,
        "n_components": len(group), "within_variance_us2": within,
        "between_variance_us2": between, "total_variance_us2": total,
        "between_variance_fraction": between / total,
        "sd_model_mm": LOG_CONVERSION_MM_PER_US * np.sqrt(total),
    })
variance_parts = pd.DataFrame(variance_rows)
variance_parts.to_csv(RESULTS / "variance_decomposition.csv", index=False)

# Quantify disagreement between the observed integer-time distribution and the
# fitted model. Total variation (TV) is half the sum of absolute probability
# differences, including predicted probability beyond the observed range.
# This is a descriptive fit diagnostic without an iid-based significance test.
fit_diagnostic_rows = []
for filename, nominal_mm, _ in RUNS:
    samples = fit_samples[filename].astype(int)
    t_grid = np.arange(samples.min(), samples.max() + 1)
    counts = np.bincount(samples - t_grid[0], minlength=len(t_grid))
    empirical = counts / len(samples)
    model = mixture_models[filename]
    predicted = np.zeros(len(t_grid), dtype=float)
    for weight, mu, cov in zip(model.weights_, model.means_.ravel(),
                               model.covariances_.reshape(-1)):
        sd = np.sqrt(cov)
        predicted += weight * (norm.cdf(t_grid + .5, mu, sd)
                               - norm.cdf(t_grid - .5, mu, sd))
    tail_mass = max(0., 1. - predicted.sum())
    tv_distance = .5 * (np.abs(empirical - predicted).sum() + tail_mass)
    fit_diagnostic_rows.append({
        "run": filename, "nominal_mm": nominal_mm,
        "selected_k": model.n_components,
        "total_variation": tv_distance,
        "model_tail_probability": tail_mass,
    })
fit_diagnostics = pd.DataFrame(fit_diagnostic_rows)
fit_diagnostics.to_csv(RESULTS / "fit_diagnostics.csv", index=False)

# Serially adjacent samples may be correlated. For the plotted median we use
# circular moving-block bootstrap of 50 parsed events (~1 s at nominal 50 Hz),
# preserving failed-event positions. The percentile interval describes
# resampling variability conditional on each recorded run; dynamic-run
# changes and ruler systematic error are outside its scope.
def median_block_ci(df, block_size=50, repetitions=700, seed=2026):
    values = np.where(df.is_valid.to_numpy(),
                      df.distance_mm.to_numpy(dtype=float), np.nan)
    n = len(values)
    rng = np.random.default_rng(seed)
    nblocks = int(np.ceil(n / block_size))
    offsets = np.arange(block_size)
    medians = np.empty(repetitions)
    for i in range(repetitions):
        starts = rng.integers(0, n, size=nblocks)
        ids = ((starts[:, None] + offsets) % n).reshape(-1)[:n]
        medians[i] = np.nanmedian(values[ids])
    return np.quantile(medians, [.025, .975])

ci_rows = []
for filename, nominal_mm, _ in RUNS:
    lo, hi = median_block_ci(datasets[filename], seed=2026 + nominal_mm)
    ci_rows.append({"run": filename, "nominal_mm": nominal_mm,
                    "median_mm": float(summary.loc[summary.run == filename,
                                                     "median_mm"].iloc[0]),
                    "ci_low_mm": lo, "ci_high_mm": hi})
median_ci = pd.DataFrame(ci_rows)
median_ci.to_csv(RESULTS / "median_block_bootstrap_ci.csv", index=False)

# A small feature in the 128-mm recording around 288--293 mm is visible
# in the observed frequencies but does not acquire its own narrow component
# in the bounded six-component GMM. Report its empirical occupancy explicitly.
weak_128 = datasets["128 abbassato da 17 un po scosso.txt"]
weak_128 = weak_128.loc[weak_128.is_valid]
weak_peak_count = int(weak_128.distance_mm.between(284, 294).sum())
weak_peak_fraction = weak_peak_count / len(weak_128)

# Strongest (largest fitted mixture weight) component, in each recording.
# This companion calibration-style view remains exploratory, especially in
# the moving runs where nominal height was not constant.
dominant_rows = []
for filename, nominal_mm, _ in RUNS:
    group = components.loc[components.run == filename]
    dominant = group.loc[group.weight.idxmax()]
    dominant_rows.append({"run": filename, "nominal_mm": nominal_mm,
                          "dominant_weight": dominant.weight,
                          "dominant_apparent_mm": dominant.mean_distance_mm,
                          "dominant_sd_mm": dominant.sd_distance_mm,
                          "n_components": int(dominant.selected_k)})
dominant_response = pd.DataFrame(dominant_rows)
dominant_response.to_csv(RESULTS / "dominant_component_response.csv", index=False)


# --- 5. Figures (all captions are in Org; none in Matplotlib) ---
plt.rcParams.update({
    "font.size": 9.4, "axes.spines.top": False,
    "axes.spines.right": False, "savefig.dpi": 190,
})
# Height uncertainty: the ruler-resolution, parallax and inclination model
# adopted explicitly in the Experimental setup section.
A_PAR = np.deg2rad(30.)
A_RULER = np.deg2rad(5.)
U_RES_MM = .5 / np.sqrt(3)
U_PAR_MM = 5. * np.sqrt((np.tan(A_PAR) - A_PAR) / A_PAR)
U_INCL_FACTOR = np.sqrt(
    .5 + np.sin(2 * A_RULER) / (4 * A_RULER)
    - (np.sin(A_RULER) / A_RULER)**2
)
def reference_uncertainty(r_mm):
    r_mm = np.asarray(r_mm, dtype=float)
    return np.sqrt(U_RES_MM**2 + U_PAR_MM**2 + (r_mm * U_INCL_FACTOR)**2)

h = summary.nominal_mm.to_numpy(dtype=float)
xx = np.linspace(48, 310, 350)
fig, ax = plt.subplots(figsize=(8.1, 4.8), constrained_layout=True)
ax.axvspan(48, 200, color="0.93", zorder=0)
ax.axvline(200, color="0.48", linestyle=":", linewidth=1.,
           label="Specified minimum range")
ax.plot(xx, xx, "--", color="0.35", lw=1.1, label="Ideal response")
ax.plot(xx, baseline_distance(xx), ":", color="0.68", lw=1.2,
        label="Exploratory floor curve")
ax.errorbar(h, summary.mean_mm, xerr=reference_uncertainty(h),
            yerr=summary.sd_mm, fmt="o", color="tab:blue", capsize=3.2,
            markersize=5, label="Mean ± sample SD")
ci_lookup = median_ci.set_index("run").loc[summary.run]
median_values = summary.median_mm.to_numpy(float)
median_err = np.vstack([
    np.maximum(0., median_values - ci_lookup.ci_low_mm.to_numpy()),
    np.maximum(0., ci_lookup.ci_high_mm.to_numpy() - median_values),
])
ax.errorbar(h, median_values, xerr=reference_uncertainty(h), yerr=median_err,
            fmt="s", color="tab:orange", capsize=3.2,
            markersize=5, label="Median ± 95% block-bootstrap CI")
ax.set(xlim=(50, 305), ylim=(45, 395),
       xlabel="Nominal reference height (mm)", ylabel="Reported distance (mm)")
ax.grid(alpha=.2)
ax.legend(loc="upper left", fontsize=8, frameon=False)
fig.savefig(FIG / "height_statistics.png")
plt.close(fig)

# Stable recordings: the very narrow time distributions have only a handful
# of resolvable timing values. Showing empirical 1-us frequencies is more
# informative than claiming they trace a continuous Gaussian distribution.
# Smooth mixture curves are shown only for the two recordings with visibly
# separated groups (131 and 188 mm), and fitted directly to individual samples.
fig, axs = plt.subplots(2, 3, figsize=(11.9, 5.5), constrained_layout=True)
fig_order = ("69.txt", "170.txt", "201.txt", "283.txt", "131.txt", "188.txt")
for ax, filename in zip(axs.flat, fig_order):
    values = fit_samples[filename]
    nominal = int(datasets[filename].nominal_mm.iloc[0])
    times, counts = np.unique(values.astype(int), return_counts=True)
    ax.bar(times, counts, width=.74, color="tab:blue", alpha=.72,
           label="Recorded 1 µs counts")
    if filename in SECONDARY_RUNS:
        # Show inferred group locations, without imposing a continuous noise
        # shape on the sharply quantized observed pulse-duration counts.
        model = mixture_models[filename]
        for j, center in enumerate(sorted(model.means_.ravel())):
            ax.axvline(center, color="tab:orange", lw=1.1, alpha=.85,
                       linestyle="--",
                       label="Estimated group centre" if j == 0 else None)
    ax.text(.97, .95, f"{nominal} mm", transform=ax.transAxes,
            ha="right", va="top", fontsize=9)
    ax.set_xlabel("ECHO duration (µs)")
    ax.set_ylabel("Count per 1 µs")
    ax.grid(alpha=.15, axis="y")
    ax.tick_params(axis="both", labelsize=8)
axs[0, 0].legend(fontsize=7.5, loc="upper left", frameon=False)
axs[1, 1].legend(fontsize=7.5, loc="upper left", frameon=False)
fig.savefig(FIG / "distributions_stable.png")
plt.close(fig)

# Moving runs: same figure shows chronological regimes and histogram peaks.
fig, axs = plt.subplots(2, 2, figsize=(12.2, 6.5), constrained_layout=True)
for row, filename in enumerate(MOVING_RUNS):
    ax_t, ax_h = axs[row]
    df = datasets[filename]
    visible = df.is_valid & (df.distance_mm <= FIT_DISTANCE_CEILING_MM)
    ax_t.scatter(df.loc[visible, "elapsed_s"],
                 df.loc[visible, "distance_mm"],
                 s=2, alpha=.33, color="tab:blue", rasterized=True)
    ax_t.axhline(200, color="0.5", linestyle=":", linewidth=1.)
    ax_t.set(xlabel="Elapsed log time (s)",
             ylabel="Reported distance (mm)", ylim=(190, 455))
    ax_t.grid(alpha=.15)
    x = fit_samples[filename]
    model = mixture_models[filename]
    d = LOG_CONVERSION_MM_PER_US * x
    bins = np.arange(195, 452, 1.)
    ax_h.hist(d, bins=bins, color="tab:blue", alpha=.4,
              label="Observed (1 mm bins)")
    pred = np.zeros(len(bins) - 1)
    centers = .5 * (bins[1:] + bins[:-1])
    for wt, mu, cov in zip(model.weights_, model.means_.ravel(),
                           model.covariances_.reshape(-1)):
        sigma = np.sqrt(cov)
        expected = len(d) * wt * np.diff(norm.cdf(
            bins / LOG_CONVERSION_MM_PER_US, mu, sigma))
        pred += expected
        ax_h.step(centers, expected, where="mid", lw=.9,
                  color="tab:orange", alpha=.55)
    ax_h.step(centers, pred, where="mid", color="0.25", lw=1.25,
              label=f"Integrated GMM (K={model.n_components})")
    if row == 1:
        # Unresolved small observed shoulder around 289 mm.
        ax_h.axvspan(284, 294, facecolor="tab:purple", alpha=.13)
        ax_h.annotate("~289 mm group", xy=(289, 24), xytext=(255, 160),
                      arrowprops={"arrowstyle": "->", "color": "0.25"},
                      fontsize=8)
    ax_h.set(xlabel="Pulse-derived distance (mm)",
             ylabel="Count per 1 mm", xlim=(195, 450))
    ax_h.grid(alpha=.15, axis="y")
    ax_t.text(.02, .95, f"({chr(97+2*row)})  {int(df.nominal_mm.iloc[0])} mm",
              transform=ax_t.transAxes, va="top", ha="left")
    ax_h.text(.98, .95, f"({chr(98+2*row)})", transform=ax_h.transAxes,
              va="top", ha="right")
    ax_h.legend(fontsize=8, frameon=False)
fig.savefig(FIG / "disturbed_time_distributions.png")
plt.close(fig)

# Small, consistently sized symbols keep the dominant responses visible;
# weights are quantified in the results table rather than encoded by area.
fig, ax = plt.subplots(figsize=(8.0, 4.3), constrained_layout=True)
ax.axvspan(48, 200, color="0.93", zorder=0)
ax.axvline(200, linestyle=":", color="0.5", lw=1.)
ax.plot(xx, xx, "--", color="0.4", lw=1., label="Ideal response")
for filename, nominal, _ in RUNS:
    group = components.loc[components.run == filename].copy()
    main_idx = group.weight.idxmax()
    other = group.drop(index=main_idx)
    if len(other):
        ax.scatter(np.full(len(other), nominal), other.mean_distance_mm,
                   s=14, color="tab:blue", alpha=.45, linewidths=.4,
                   edgecolors="tab:blue", zorder=3)
    main = group.loc[main_idx]
    ax.scatter([nominal], [main.mean_distance_mm],
               s=37, marker="D",
               facecolor=("tab:orange" if filename in MOVING_RUNS else "tab:blue"),
               edgecolor="0.22", linewidth=.55, zorder=5)
    ax.errorbar([nominal], [main.mean_distance_mm],
                xerr=reference_uncertainty(nominal),
                yerr=[[main.sd_distance_mm], [main.sd_distance_mm]],
                fmt="none", color="0.35", capsize=2., lw=.85, zorder=4)
ax.scatter([], [], s=37, marker="D", color="tab:blue", edgecolor="0.22",
           label="Dominant: nominally stationary")
ax.scatter([], [], s=37, marker="D", color="tab:orange", edgecolor="0.22",
           label="Dominant: moving")
ax.scatter([], [], s=14, color="tab:blue", alpha=.45,
           label="Other response groups")
ax.set(xlabel="Nominal reference height (mm)",
       ylabel="Apparent distance (mm)",
       xlim=(50, 305), ylim=(185, 440))
ax.grid(alpha=.2)
ax.legend(fontsize=7.4, frameon=False, loc="upper left")
fig.savefig(FIG / "dominant_components.png")
plt.close(fig)

fig, ax = plt.subplots(figsize=(8.1, 3.8), constrained_layout=True)
indices = np.arange(len(summary))
p_valid = summary.n_valid / summary.n_parsed
p_no_echo = summary.n_no_echo / summary.n_parsed
p_stuck = summary.n_stuck / summary.n_parsed
ax.bar(indices, 100 * p_valid, label="Valid (status 3)")
ax.bar(indices, 100 * p_no_echo, bottom=100 * p_valid,
       label="No echo (status 0)")
ax.bar(indices, 100 * p_stuck, bottom=100 * (p_valid+p_no_echo),
       label="Stuck (status 1)")
ax.set_xticks(indices, [str(int(v)) for v in summary.nominal_mm])
ax.set(xlabel="Nominal reference height (mm)",
       ylabel="Fraction of parsed records (%)", ylim=(0, 103))
ax.legend(ncol=3, fontsize=8, frameon=False, loc="lower center",
          bbox_to_anchor=(.5, 1.02))
ax.grid(alpha=.16, axis="y")
fig.savefig(FIG / "status_rates.png")
plt.close(fig)


# --- 6. Tables, temperature sensitivity, and parse audit ---
def fmt(x, digits=1):
    return f"{x:.{digits}f}" if pd.notna(x) else "—"

lines = [
    "| Nominal (mm) | Parsed | Valid | No echo | Stuck | Valid (%) | Mean (mm) | SD (mm) | Median (mm) | Bias (mm) |",
    "|-",
]
for _, s in summary.iterrows():
    lines.append("| " + " | ".join([
        str(int(s.nominal_mm)), str(int(s.n_parsed)), str(int(s.n_valid)),
        str(int(s.n_no_echo)), str(int(s.n_stuck)),
        fmt(s.valid_fraction * 100, 1), fmt(s.mean_mm, 2),
        fmt(s.sd_mm, 2), fmt(s.median_mm, 1), fmt(s.bias_mm, 2),
    ]) + " |")
(RESULTS / "summary_table.org").write_text("\n".join(lines)+"\n", encoding="utf-8")

# Tables describing actual model outputs, with the same parameters used in
# the figures (rather than a second, unrelated modal-band calculation).
mode_lines = [
    "| Nominal (mm) | Dominant component (mm) | SD (mm) | Weight (%) | Components |",
    "|-",
]
for _, s in dominant_response.iterrows():
    mode_lines.append("| " + " | ".join([
        str(int(s.nominal_mm)), fmt(s.dominant_apparent_mm, 2),
        fmt(s.dominant_sd_mm, 2), fmt(s.dominant_weight * 100, 1),
        str(int(s.n_components)),
    ]) + " |")
(RESULTS / "dominant_table.org").write_text("\n".join(mode_lines)+"\n", encoding="utf-8")

# Expected bias if the conversion assumes c=343 m/s and actual c(T) differs.
def c_air(T_C):
    return 331.45 + .607 * np.asarray(T_C)
sensitivity_rows = []
for temperature in (15., 20., 25.):
    c_true = float(c_air(temperature))
    for true_h in (200., 283.):
        apparent = 343.0 / c_true * true_h
        sensitivity_rows.append({
            "actual_air_temperature_C": temperature,
            "assumed_sound_speed_m_s": 343.0,
            "actual_sound_speed_m_s_model": c_true,
            "true_distance_mm": true_h,
            "reported_distance_mm_model": apparent,
            "distance_bias_mm_model": apparent - true_h,
        })
pd.DataFrame(sensitivity_rows).to_csv(
    RESULTS / "temperature_sensitivity.csv", index=False)

qc_rows = []
for filename, _, _ in RUNS:
    df = datasets[filename]
    qc_rows.append({
        "run": filename, "raw_sha256": df.attrs["sha256"],
        "complete_records": df.attrs["complete_records"],
        "echo_tags": df.attrs["echo_tags"],
        "incomplete_or_damaged_echo_tags": df.attrs["damaged_or_incomplete_echo_tags"],
    })
pd.DataFrame(qc_rows).to_csv(RESULTS / "parse_audit.csv", index=False)
print(f"Parsed {len(all_events):,} complete events, "
      f"{int(all_events.is_valid.sum()):,} valid, across {len(RUNS)} files.")
print("All valid pulse-to-distance conversions checked.")
print("Median 95% block-bootstrap intervals:")
print(median_ci[["nominal_mm", "median_mm", "ci_low_mm", "ci_high_mm"]].
      to_string(index=False))
print("Fitted component count and between-regime variance:")
print(variance_parts[["nominal_mm", "n_components", "between_variance_fraction"]].
      to_string(index=False))
print(f"128 mm empirical 284--294 mm group: {weak_peak_count} "
      f"({100 * weak_peak_fraction:.2f}% of valid records)")

