"""Cấu hình, kiểm tra provenance và tiện ích tệp phục vụ chuẩn bị dữ liệu."""

from dataclasses import dataclass, field
import hashlib
import json
import math
import re
import pandas as pd
from pathlib import Path

import yaml

FEATURES = ("pm25", "temperature", "humidity")
LOOKBACK_HOURS = 24
HORIZON_HOURS = 24
MAX_GAP_HOURS = 3


@dataclass(frozen=True)
class PreparationConfig:
    """Hợp đồng dữ liệu dùng chung bất biến và các đường dẫn của dự án."""

    input_csv: Path = Path("data/Hanoi_aqi/hanoi_aqi_ml_ready_fixed.csv")
    processed: Path = Path("data/processed/pm25_24x24_20251121_linear_v2")
    tables: Path = Path("reports/tables")
    figures: Path = Path("reports/figures")
    timezone: str = "UTC"
    features: tuple = FEATURES
    lookback: int = LOOKBACK_HOURS
    horizon: int = HORIZON_HOURS
    missing_method: str = "linear"
    max_gap_hours: int = MAX_GAP_HOURS
    train_fraction: float = 0.70
    validation_fraction: float = 0.15
    allowed_sources: tuple = ("OpenAQ",)
    provenance: dict = field(default_factory=dict)
    config_path: Path | None = None
    start: str | None = None
    end_exclusive: str | None = None


def load_config(path: Path) -> PreparationConfig:
    """Tải YAML và phân giải đường dẫn tương đối với thư mục gốc dự án.

    Trả về cấu hình đã được xác thực. Section sai định dạng hoặc hợp đồng dữ liệu
    không tương thích sẽ gây ValueError, tệp không đọc được sẽ gây OSError.
    """
    path = Path(path).resolve()
    try:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        raise ValueError("Invalid YAML") from exc
    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise ValueError("Configuration root must be a mapping")
    for name in ("paths", "data", "missing", "split", "provenance"):
        if name in raw and not isinstance(raw[name], dict):
            raise ValueError(f"{name} must be a mapping")

    base = path.parent.parent
    paths = raw.get("paths", {})
    data = raw.get("data", {})
    missing = raw.get("missing", {})
    split = raw.get("split", {})
    provenance = raw.get("provenance", {})
    if set(missing) - {"method", "max_gap_hours"}:
        raise ValueError("Unknown missing policy configuration")
    evidence_raw = provenance.get("evidence", {})
    allowed_sources = provenance.get("allowed_sources", ["OpenAQ"])
    if not isinstance(evidence_raw, dict):
        raise ValueError("evidence must be a mapping")
    if not isinstance(allowed_sources, list) or any(
        not isinstance(source, str) for source in allowed_sources
    ):
        raise ValueError("allowed_sources must be a list of strings")
    if not isinstance(data.get("features", list(FEATURES)), list):
        raise ValueError("features must be a list")
    if any(not isinstance(value, str) or not value.strip() for value in paths.values()):
        raise ValueError("paths must be nonempty strings")
    evidence = dict(evidence_raw)
    if "document" in evidence and not isinstance(evidence["document"], str):
        raise ValueError("evidence document must be a string")
    if evidence.get("document"):
        evidence["document"] = str((base / evidence["document"]).resolve())

    config = PreparationConfig(
        input_csv=(
            base / paths.get("input_csv", str(PreparationConfig.input_csv))
        ).resolve(),
        processed=(
            base / paths.get("processed", str(PreparationConfig.processed))
        ).resolve(),
        tables=(base / paths.get("tables", str(PreparationConfig.tables))).resolve(),
        figures=(base / paths.get("figures", str(PreparationConfig.figures))).resolve(),
        timezone=data.get("timezone", "UTC"),
        features=tuple(data.get("features", FEATURES)),
        lookback=data.get("lookback", LOOKBACK_HOURS),
        horizon=data.get("horizon", HORIZON_HOURS),
        missing_method=missing.get("method", "linear"),
        max_gap_hours=missing.get("max_gap_hours", MAX_GAP_HOURS),
        train_fraction=split.get("train_fraction", 0.7),
        validation_fraction=split.get("validation_fraction", 0.15),
        allowed_sources=tuple(allowed_sources),
        provenance=evidence,
        config_path=path,
        start=data.get("start"),
        end_exclusive=data.get("end_exclusive"),
    )
    validate_config(config)
    return config


def parse_time_bound(value):
    """Chuyển mốc ISO có múi giờ về UTC và từ chối mốc không tròn giờ."""
    if value is None:
        return None
    if not isinstance(value, str) or not re.fullmatch(
        r"\d{4}-\d{2}-\d{2}T\d{2}:00:00(?:Z|[+-]\d{2}:\d{2})", value
    ):
        raise ValueError("Time bound must be an ISO full-hour timestamp with timezone")
    try:
        timestamp = pd.Timestamp(value).tz_convert("UTC")
    except (ValueError, TypeError) as exc:
        raise ValueError("Invalid time bound") from exc
    if (
        timestamp.minute
        or timestamp.second
        or timestamp.microsecond
        or timestamp.nanosecond
    ):
        raise ValueError("Time bound must resolve to a full UTC hour")
    return timestamp


def validate_config(config: PreparationConfig) -> None:
    """Từ chối cấu hình trực tiếp hoặc cấu hình tạo từ YAML không hợp lệ trước khi ghi đầu ra."""
    if not isinstance(config, PreparationConfig):
        raise ValueError("Invalid configuration")
    start = parse_time_bound(config.start)
    end = parse_time_bound(config.end_exclusive)
    if start is not None and end is not None and start >= end:
        raise ValueError("Start must precede exclusive end")
    if (
        config.features != FEATURES
        or type(config.lookback) is not int
        or config.lookback != LOOKBACK_HOURS
        or type(config.horizon) is not int
        or config.horizon != HORIZON_HOURS
        or config.timezone != "UTC"
    ):
        raise ValueError(
            "Contract requires UTC and 24x3 to 24 with feature order pm25, temperature, humidity"
        )
    if config.missing_method != "linear":
        raise ValueError("Only linear interpolation is supported")
    if (
        type(config.max_gap_hours) is not int
        or not 0 <= config.max_gap_hours <= MAX_GAP_HOURS
    ):
        raise ValueError("Invalid fill limit")
    fractions = (config.train_fraction, config.validation_fraction)
    if (
        any(
            type(value) not in (int, float) or not math.isfinite(value)
            for value in fractions
        )
        or not 0 < fractions[0] < 1
        or not 0 < fractions[1] < 1 - fractions[0]
    ):
        raise ValueError("Invalid split configuration")
    if (
        not isinstance(config.allowed_sources, (tuple, list))
        or not config.allowed_sources
        or any(
            not isinstance(source, str) or not source.strip()
            for source in config.allowed_sources
        )
    ):
        raise ValueError("Invalid source list")
    if not isinstance(config.provenance, dict):
        raise ValueError("Invalid provenance")
    for path in (config.input_csv, config.processed, config.tables, config.figures):
        if not isinstance(path, (str, Path)) or not str(path).strip():
            raise ValueError("Invalid path")


def provenance_verified(config: PreparationConfig) -> bool:
    """Kiểm tra cấu trúc bằng chứng và sự tồn tại của tệp, còn tính xác thực cần được rà soát."""
    evidence = config.provenance
    if not isinstance(evidence, dict):
        return False
    fields = ("document", "station", "units", "timezone", "processing", "availability")
    if any(
        not isinstance(evidence.get(key), str) or not evidence[key].strip()
        for key in fields
    ):
        return False
    sources = evidence.get("sources")
    if (
        not isinstance(sources, list)
        or not sources
        or any(not isinstance(source, str) or not source.strip() for source in sources)
    ):
        return False
    return (
        Path(evidence["document"]).is_file()
        and evidence["units"] in ("ug/m3", "µg/m³")
        and evidence["timezone"] == config.timezone
        and set(config.allowed_sources).issubset(sources)
    )


def validate_output_paths(config: PreparationConfig) -> None:
    """Từ chối các đường dẫn đầu ra sau phân giải bị chồng lấn với nguồn, bằng chứng, cấu hình hoặc lẫn nhau."""
    source = Path(config.input_csv).resolve()
    protected = [source.parent, source]
    if config.config_path:
        protected.append(Path(config.config_path).resolve())
    if (
        isinstance(config.provenance, dict)
        and isinstance(config.provenance.get("document"), str)
        and config.provenance["document"].strip()
    ):
        protected.append(Path(config.provenance["document"]).resolve())
    outputs = [
        Path(path).resolve()
        for path in (config.tables, config.figures, config.processed)
    ]

    # Từ chối cả quan hệ thư mục cha lẫn thư mục con trước khi tạo thư mục.
    def overlaps(first, second):
        return first == second or first in second.parents or second in first.parents

    if any(overlaps(output, item) for output in outputs for item in protected) or any(
        overlaps(first, second)
        for i, first in enumerate(outputs)
        for second in outputs[i + 1 :]
    ):
        raise ValueError("Output paths overlap protected inputs or each other")


def sha256(path: Path) -> str:
    """Trả về mã băm SHA-256 của tệp mà không thay đổi tệp."""
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path: Path, value) -> None:
    """Ghi JSON vào đường dẫn đã được bên gọi xác thực và giữ nguyên Unicode."""
    Path(path).write_text(
        json.dumps(value, indent=2, default=str, ensure_ascii=False), encoding="utf-8"
    )
