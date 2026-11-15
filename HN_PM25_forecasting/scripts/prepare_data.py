"""Điểm vào dòng lệnh để kiểm tra và chuẩn bị dữ liệu PM2.5."""

import argparse
from pathlib import Path

from src.data_pipeline import run_preparation
from src.utils import load_config


def main(argv: list[str] | None = None) -> int:
    """Chạy chế độ đã chọn, trả về 0 khi thành công và 1 khi thất bại."""
    parser = argparse.ArgumentParser(
        description="Audit or prepare hourly PM2.5 forecasting data"
    )
    parser.add_argument("--config", type=Path, default=Path("configs/common.yaml"))
    parser.add_argument("--mode", choices=["audit", "prepare"], required=True)
    args = parser.parse_args(argv)
    try:
        result = run_preparation(load_config(args.config), args.mode)
    except (ValueError, OSError) as exc:
        print(f"Error: {exc}")
        return 1
    print(f"{result.status}: {result.report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
