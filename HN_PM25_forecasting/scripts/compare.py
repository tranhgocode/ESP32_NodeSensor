"""Compare exactly three models and three seeds, freezing selection before test."""

import argparse
from pathlib import Path
import numpy as np
import pandas as pd

from src.metrics import summarize_seeds, compute_forecast_metrics
from src.utils import (
    MODEL_NAMES,
    dataset_fingerprint,
    read_json,
    sha256,
    validate_locked_run,
    validate_run_id,
    write_run_json,
)

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SEEDS = (42, 52, 62)


def compare_runs(run_id: str, split: str, freeze_selection: bool = False) -> Path:
    validate_run_id(run_id)
    if (
        split not in ("validation", "test")
        or freeze_selection
        and split != "validation"
    ):
        raise ValueError("Selection can only be frozen from validation")
    root = PROJECT_ROOT.resolve()
    tables = root / "reports/tables" / f"models_{run_id}"
    selection_path = tables / "selection.json"
    if freeze_selection and selection_path.exists():
        raise FileExistsError(f"Selection already exists: {selection_path}")
    records, horizon_rows, entries, summaries = [], [], [], []
    reference = None
    fingerprints = set()
    common_training = None
    baseline_records = {name: [] for name in ("persistence", "seasonal_naive")}
    for name in MODEL_NAMES:
        model_rows = []
        for seed in SEEDS:
            run = root / "runs" / name / run_id / f"seed_{seed}"
            meta = read_json(run / "run_metadata.json")
            if (
                meta.get("status") != "completed"
                or meta.get("smoke") is not False
                or meta.get("seed") != seed
                or meta.get("model_name") != name
            ):
                raise ValueError("Comparison requires nine completed official runs")
            if split == "test":
                validate_locked_run(run, selection_path)
            snapshot = read_json(run / "config_snapshot.json")
            training = snapshot["training"]
            if sorted(training["seeds"]) != list(SEEDS):
                raise ValueError("Comparison requires the predefined three seeds")
            if common_training is None:
                common_training = training
            if training != common_training:
                raise ValueError("Training protocols differ")
            if (
                sha256(run / "best.keras") != meta["checkpoint_sha256"]
                or sha256(run / "config_snapshot.json") != meta["config_sha256"]
                or dataset_fingerprint(Path(meta["processed_dir"]))
                != meta["dataset_fingerprint"]
            ):
                raise ValueError("Run config, checkpoint or dataset changed")
            evaluation = run / "evaluation" / split
            saved = read_json(evaluation / "metrics.json")
            if (
                saved["dataset_fingerprint"] != meta["dataset_fingerprint"]
                or saved["checkpoint_sha256"] != meta["checkpoint_sha256"]
                or saved["config_sha256"] != meta["config_sha256"]
                or saved["predictions_sha256"] != sha256(evaluation / "predictions.npz")
            ):
                raise ValueError("Evaluation does not match run")
            with np.load(evaluation / "predictions.npz", allow_pickle=False) as a:
                alignment = tuple(
                    a[k] for k in ("origins", "target_times", "y_true_raw")
                )
                if reference is None:
                    reference = alignment
                if any(not np.array_equal(x, y) for x, y in zip(reference, alignment)):
                    raise ValueError(
                        "Forecast origins, target timestamps or labels differ"
                    )
                # Recompute the primary metrics rather than trusting an edited score file.
                actual = compute_forecast_metrics(a["y_true_raw"], a["y_pred_raw"])
            if actual != saved["model"]:
                raise ValueError("Evaluation scores changed")
            fingerprints.add(meta["dataset_fingerprint"])
            row = {
                "model": name,
                "seed": seed,
                **actual["overall"],
                "parameter_count": meta["parameter_count"],
                "training_seconds": meta["training_seconds"],
                "device": meta["device_requested"],
                "platform": meta["platform"],
            }
            records.append(row)
            model_rows.append(row)
            horizon_rows.extend(
                {"model": name, "seed": seed, **h} for h in actual["per_horizon"]
            )
            for baseline, metrics in saved["baselines"].items():
                baseline_records[baseline].append(metrics)
            entries.append(
                {
                    "model_name": name,
                    "seed": seed,
                    "run_dir": run.relative_to(root).as_posix(),
                    "config_sha256": meta["config_sha256"],
                    "checkpoint_sha256": meta["checkpoint_sha256"],
                }
            )
        summaries.append(
            {
                "model": name,
                **summarize_seeds(model_rows),
                "parameter_count": model_rows[0]["parameter_count"],
                "training_seconds_mean": float(
                    np.mean([r["training_seconds"] for r in model_rows])
                ),
                "n_pairs": model_rows[0]["n_pairs"],
            }
        )
    if len(fingerprints) != 1:
        raise ValueError("Dataset fingerprints differ")
    for name, values in baseline_records.items():
        if any(m != values[0] for m in values):
            raise ValueError("Baselines differ across runs")
        summaries.append(
            {
                "model": name,
                **summarize_seeds([values[0]["overall"]] * 3),
                "parameter_count": 0,
                "training_seconds_mean": 0,
                "n_pairs": values[0]["overall"]["n_pairs"],
            }
        )
        for seed in SEEDS:
            records.append({"model": name, "seed": seed, **values[0]["overall"]})
            horizon_rows.extend(
                {"model": name, "seed": seed, **h} for h in values[0]["per_horizon"]
            )
    tables.mkdir(parents=True, exist_ok=True)
    split_dir = tables / split
    # Reports can be refreshed after revalidation, the selection itself is immutable.
    split_dir.mkdir(exist_ok=True)
    pd.DataFrame(records).to_csv(split_dir / "per_run.csv", index=False)
    pd.DataFrame(horizon_rows).to_csv(split_dir / "per_horizon.csv", index=False)
    pd.DataFrame(summaries).to_csv(split_dir / "summary.csv", index=False)
    if freeze_selection:
        chosen = min(
            (r for r in summaries if r["model"] in MODEL_NAMES),
            key=lambda r: (r["mae_mean"], r["parameter_count"], r["model"]),
        )
        write_run_json(
            selection_path,
            {
                "run_id": run_id,
                "primary_metric": "mae",
                "direction": "min",
                "units": "ug/m3",
                "primary_model": chosen["model"],
                "representative_seed": 42,
                "dataset_fingerprint": fingerprints.pop(),
                "runs": entries,
            },
            exclusive=True,
        )
    from src.plots import plot_model_comparison

    plot_model_comparison(root, run_id, split, pd.DataFrame(horizon_rows))
    return split_dir / "summary.csv"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--split", choices=("validation", "test"), required=True)
    parser.add_argument("--freeze-selection", action="store_true")
    args = parser.parse_args()
    try:
        print(compare_runs(args.run_id, args.split, args.freeze_selection))
    except (ValueError, OSError, RuntimeError) as exc:
        parser.exit(1, f"{exc}\n")


if __name__ == "__main__":
    main()
