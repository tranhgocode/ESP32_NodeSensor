"""Prepare complete 24-hour histories and four hourly PM2.5 targets."""

import argparse
from pathlib import Path
import sys
from src.data_pipeline import run_preparation
from src.utils import PreparationConfig

DEFAULT_MAX_INTERPOLATION_GAP_HOURS = 3


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Hourly source CSV")
    parser.add_argument("--output", type=Path, required=True, help="New dataset directory")
    parser.add_argument("--train-split", type=float, default=0.70, help="Train fraction, default .70")
    parser.add_argument("--val-split", type=float, default=0.15, help="Validation fraction, default .15")
    parser.add_argument("--test-split", type=float, default=0.15, help="Test fraction, default .15")
    parser.add_argument("--shuffle", action="store_true", help="Shuffle train samples only, seed 42")
    parser.add_argument(
        "--max-interpolation-gap-hours",
        type=int,
        default=DEFAULT_MAX_INTERPOLATION_GAP_HOURS,
        help=(
            "Maximum consecutive missing history hours to interpolate "
            "(default: %(default)s, 0: forward fill only)"
        ),
    )
    
    args = parser.parse_args(argv)
    
    try:
        config = PreparationConfig(
            input_csv=args.input,
            processed=args.output,
            train_fraction=args.train_split,
            validation_fraction=args.val_split,
            test_fraction=args.test_split,
            shuffle=args.shuffle,
            max_interpolation_gap_hours=args.max_interpolation_gap_hours,
        )
        result = run_preparation(config)
    except (ValueError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    
    print(f"Dataset: {result.archive_path}")
    print(f"Samples: {result.counts}")
    print(f"Reports: {result.reports_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
