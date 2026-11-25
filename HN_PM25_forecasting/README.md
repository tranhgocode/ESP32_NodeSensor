# Dự báo PM2.5 tại Hà Nội

## Tổng quan

Dự án dự báo nồng độ PM2.5 (µg/m³) tại Hà Nội ở bốn mốc **1, 2, 3 và 4 giờ tiếp theo**. Mỗi mẫu đầu vào gồm **24 mốc cách nhau 1 giờ**, với ba đặc trưng **PM2.5, nhiệt độ và độ ẩm**.

Dự án hỗ trợ huấn luyện và đánh giá LSTM, SE-LSTM, CNN-LSTM và Conv1D-GRU. Hai phương pháp đối chứng là Persistence và Seasonal naive.

### Quy trình xử lý

1. Đọc CSV, kiểm tra mốc thời gian và giá trị quan sát, sau đó đưa dữ liệu về lưới theo giờ UTC.
2. Chia miền thời gian theo tỷ lệ mặc định **70% huấn luyện, 15% xác thực và 15% kiểm thử**. Tạo cửa sổ đầu vào **24 × 3** và giữ các cửa sổ có cả bốn nhãn PM2.5 thuộc cùng một tập.
3. Xử lý dữ liệu thiếu trong lịch sử đầu vào. Script chuẩn bị dữ liệu mặc định nội suy tuyến tính khoảng thiếu tối đa 3 giờ khi đã có hai đầu mút, sau đó dùng giá trị hợp lệ gần nhất trước đó cho các khoảng còn thiếu. Loại cửa sổ nếu đầu vào vẫn thiếu hoặc thiếu bất kỳ nhãn tương lai nào.
4. Tính tham số chuẩn hóa từ tập huấn luyện và áp dụng cho cả ba tập. Huấn luyện mô hình, lưu checkpoint tốt nhất theo hàm mất mát trên tập xác thực.
5. Đổi dự báo về µg/m³ để tính chỉ số chung và theo từng mốc. Có thể xuất checkpoint sang TFLite.

### Cấu trúc dự án

```text
HN_PM25_forecasting/
├── configs/                     # Cấu hình dữ liệu, huấn luyện và các mô hình
├── data/
│   ├── Hanoi_aqi_2/             # CSV đầu vào theo cấu hình hiện tại
│   └── processed/              # Bộ dữ liệu đã chia tập, tham số chuẩn hóa và báo cáo
├── notebooks/                   # Khám phá dữ liệu và so sánh kết quả
├── scripts/
│   ├── prepare_data.py          # Chuẩn bị dữ liệu
│   ├── train.py                 # Huấn luyện mô hình
│   ├── evaluate.py              # Đánh giá checkpoint
│   ├── compare.py               # Tổng hợp kết quả các lượt chạy
│   └── export.py                # Xuất mô hình TFLite
├── src/
│   ├── models/                  # Kiến trúc các mô hình
│   ├── data_pipeline.py         # Chia tập và tạo cửa sổ
│   ├── preprocessing.py         # Xử lý dữ liệu thiếu và chuẩn hóa
│   ├── baselines.py             # Persistence và Seasonal naive
│   └── metrics.py               # Tính các chỉ số đánh giá
├── runs/                        # Checkpoint, lịch sử huấn luyện và bản xuất
├── visualizations/              # Biểu đồ đánh giá
└── requirements.txt             # Các thư viện Python
```
