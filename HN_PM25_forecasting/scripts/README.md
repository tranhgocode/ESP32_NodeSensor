# Hướng dẫn chạy dự báo PM2.5

## 1. Tổng quan

Thư mục `scripts/` có ba script chính. Chạy theo thứ tự **chuẩn bị dữ liệu → huấn luyện → đánh giá**. Mô hình dùng dữ liệu 24 giờ gần nhất để dự báo PM2.5 trong 4 giờ tiếp theo.

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
