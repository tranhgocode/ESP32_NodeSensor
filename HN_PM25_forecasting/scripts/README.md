# Hướng dẫn

## 1. Tổng quan

Quy trình trong `scripts/` đi từ **chuẩn bị dữ liệu → huấn luyện → đánh giá → xuất mô hình**. Mô hình dùng dữ liệu 24 giờ gần nhất để dự báo PM2.5 trong 4 giờ tiếp theo.

Mở PowerShell tại thư mục gốc của kho mã. Chuyển vào thư mục dự án và cài thư viện nếu cần.

```powershell
cd HN_PM25_forecasting
python -m pip install -r requirements.txt
```

## 2. Chuẩn bị dữ liệu với `prepare_data.py`

Nếu đã có thư mục `data/processed/pm25_24x4_complete`, chuyển sang mục 3. Nếu chưa có, chạy lệnh dưới đây để chia CSV theo thời gian thành ba tập train, validation và test.

```powershell
python -m scripts.prepare_data --input data/Hanoi_aqi/hanoi_aqi_ml_ready_fixed.csv --output data/processed/pm25_24x4_complete_v1
```

| Tham số                                        | Cách dùng                                                                                                           |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `--input`                                      | Bắt buộc. Đường dẫn tới CSV theo giờ.                                                                               |
| `--output`                                     | Bắt buộc. Thư mục mới để lưu bộ dữ liệu. Nếu đổi đường dẫn, cập nhật `paths.processed` trong `configs/common.yaml`. |
| `--train-split`, `--val-split`, `--test-split` | Tỷ lệ chia, mặc định lần lượt là `0.70`, `0.15`, `0.15`. Tổng phải bằng `1`.                                        |
| `--shuffle`                                    | Thêm cờ này nếu muốn xáo trộn thứ tự mẫu trong tập train.                                                           |

## 3. Huấn luyện với `train.py`

Lệnh mẫu huấn luyện LSTM tối đa 10 epoch. Mô hình tốt nhất được lưu tại `runs/lstm/lstm-demo/seed_42/best.keras`.

```powershell
python -m scripts.train --common configs/common.yaml --model lstm --name lstm-demo --seed 42 --epochs 10
```

| Tham số                                  | Cách dùng                                                                                                  |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `--common`                               | Bắt buộc. Tệp cấu hình chung, ví dụ `configs/common.yaml`.                                                 |
| `--model`                                | Bắt buộc. Chọn `lstm`, `cnn_lstm`, `conv1d_gru`, `se_lstm` hoặc đường dẫn YAML của mô hình.                |
| `--name`                                 | Bắt buộc. Tên lượt chạy. Chọn tên mới nếu đã chạy ví dụ này và dùng tên đó ở mục 4.                        |
| `--seed`                                 | Chọn seed cho một lượt huấn luyện. Nếu bỏ qua, dùng danh sách seed trong YAML.                             |
| `--epochs`, `--batch-size`, `--patience` | Số epoch tối đa, số mẫu mỗi batch và số epoch chờ trước khi dừng sớm. Nếu bỏ qua, dùng giá trị trong YAML. |

## 4. Đánh giá với `evaluate.py`

Chạy lệnh đầu để lưu chỉ số và dự báo vào thư mục `evaluation/validation/` của lượt chạy. Lệnh thứ hai tự dùng tập test và lưu biểu đồ vào `visualizations/lstm-demo/`.

```powershell
python -m scripts.evaluate --run-dir runs/lstm/lstm-demo/seed_42 --split validation
python -m scripts.evaluate --checkpoint runs/lstm/lstm-demo/seed_42/best.keras --output-dir visualizations/lstm-demo
```

| Tham số        | Cách dùng                                                                                      |
| -------------- | ---------------------------------------------------------------------------------------------- |
| `--run-dir`    | Chọn thư mục `seed_...` để tính chỉ số. Đi kèm `--split`.                                      |
| `--split`      | Chỉ dùng với `--run-dir`. Chọn `validation` hoặc `test`.                                       |
| `--selection`  | Chỉ dùng với `--run-dir --split test`. Bắt buộc truyền `selection.json` đã chốt từ validation. |
| `--checkpoint` | Chọn `best.keras` hoặc thư mục chứa tệp này để vẽ biểu đồ test.                                |
| `--output-dir` | Dùng với `--checkpoint` để chọn nơi lưu biểu đồ. Mặc định là `visualizations/`.                |
| `--common`     | Mặc định là `configs/common.yaml`. Nếu dùng tệp khác khi huấn luyện, truyền cùng tệp ở đây.    |

## 5. Xuất mô hình TFLite với `export.py`

Chạy từ thư mục `HN_PM25_forecasting`. Lệnh đầu xuất bản `float32` vào `runs/exports/`. Lệnh thứ hai xuất bản `int8` vào thư mục riêng để giữ cả hai kết quả.

```powershell
python -m scripts.export --checkpoint runs/conv1d_gru/conv1d-gru-test/seed_42
python -m scripts.export --checkpoint runs/conv1d_gru/conv1d-gru-test/seed_42 --quantization int8 --output-dir runs/exports/conv1d-gru-seed42-int8
```

| Tham số | Cách dùng |
| --- | --- |
| `--checkpoint` | Bắt buộc. Đường dẫn tới tệp `.keras` hoặc thư mục chứa `best.keras`. |
| `--output-dir` | Mặc định là `runs/exports/`. Chọn thư mục mới hoặc thư mục trống. Nếu thư mục đích đã có nội dung, script báo lỗi để tránh ghi đè. |
| `--quantization` | Chọn `float32` (mặc định) hoặc `int8`. Bản `int8` dùng tối đa 200 cửa sổ train để hiệu chuẩn, với đầu vào và đầu ra kiểu `int8`. |
| `--processed-dir` | Chỉ định thư mục dữ liệu gốc, ví dụ `data/processed/pm25_24x4_complete_v1`. Mặc định lấy `processed_dir` từ `run_metadata.json` cạnh checkpoint. Truyền tham số này nếu đã di chuyển dữ liệu hoặc checkpoint thiếu thông tin đường dẫn. |

Thư mục xuất chứa ba tệp.

- `model.tflite` chứa mô hình đã chuyển đổi.
- `scalers.json` chứa tham số dùng để chuẩn hóa đầu vào và đổi dự báo về `µg/m³`.
- `model_info.json` ghi kiểu và kích thước tensor, danh sách toán tử, sai lệch giữa dự báo Keras và TFLite theo từng giờ trên tập validation, cùng `scale` và `zero_point` cho bản `int8`.

Khi tích hợp trên ESP32-S3, chuẩn bị đầu vào `[1, 24, 3]` gồm 24 giờ theo thứ tự `pm25`, `temperature`, `humidity`. Đầu ra `[1, 4]` tương ứng với bốn giờ tiếp theo. Với bản `int8`, lượng tử hóa dữ liệu sau khi chuẩn hóa và giải lượng tử đầu ra trước khi đổi về `µg/m³`.
