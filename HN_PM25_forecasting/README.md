# Dự báo PM2.5 tại Hà Nội

## Tổng quan

Dự án xây dựng bộ dữ liệu chuỗi thời gian để dự báo nồng độ PM2.5 tại Hà Nội. Mỗi mẫu sử dụng **24 giờ lịch sử** gồm PM2.5, nhiệt độ và độ ẩm để dự báo **PM2.5 của 24 giờ tiếp theo**.

Phần đã triển khai tập trung vào kiểm tra và tiền xử lý dữ liệu. Bộ dữ liệu đầu ra hướng đến việc huấn luyện, so sánh các mô hình Conv1D-GRU, LSTM và SE-LSTM trên cùng cách chia dữ liệu. Mã huấn luyện và đánh giá mô hình chưa được triển khai.

### Quy trình xử lý

- Đọc CSV, lọc khoảng thời gian, kiểm tra cột và mốc thời gian, phát hiện bản ghi trùng mâu thuẫn và giá trị không hợp lệ.
- Đưa dữ liệu về lưới theo giờ, thống kê các ô thiếu, giờ thiếu và mức độ bao phủ theo thời gian, nguồn dữ liệu.
- Chia miền nhãn theo thứ tự thời gian thành **train 70%, validation 15%, test 15%**. Tỷ lệ này áp dụng trên lưới thời gian, số mẫu hợp lệ sau xử lý có thể khác tỷ lệ trên.
- Tạo cửa sổ đầu vào 24 giờ, chỉ giữ mẫu có đủ điều kiện về quan sát và nhãn tương lai.
- Nội suy tuyến tính các khoảng thiếu tối đa **3 giờ liên tiếp** trong từng cửa sổ lịch sử. Hai mốc neo phải là quan sát gốc đáng tin cậy trong cửa sổ và không muộn hơn thời điểm dự báo. Không ngoại suy, không điền nhãn tương lai, thời điểm dự báo phải có đủ quan sát gốc.
- Khớp bộ chuẩn hóa bằng quan sát gốc đáng tin cậy thuộc miền train, sau đó áp dụng cho cả ba tập.
- Xuất bảng kiểm tra, biểu đồ và bộ dữ liệu dùng chung khi đủ điều kiện xác minh nguồn.

Cấu hình hiện tại chọn dữ liệu trước `2025-11-22T00:00:00Z`, tương ứng đến hết ngày 21/11/2025 theo giả định UTC. Múi giờ và nguồn quan sát cần được xác minh trước khi dùng cho huấn luyện.

### Cấu trúc dự án

```text
HN_PM25_forecasting/
├── configs/common.yaml          # Cấu hình dữ liệu, chia tập và đường dẫn
├── data/Hanoi_aqi/              # CSV đầu vào cần cung cấp
├── notebooks/01_explore_data.ipynb
├── scripts/prepare_data.py      # Điểm chạy từ terminal
├── src/
│   ├── data_pipeline.py         # Kiểm tra dữ liệu, chia tập và tạo cửa sổ
│   ├── preprocessing.py         # Nội suy và chuẩn hóa
│   ├── plots.py                 # Vẽ biểu đồ
│   └── utils.py                 # Đọc và kiểm tra cấu hình
├── reports/tables/              # Bảng kiểm tra CSV và báo cáo JSON
├── reports/figures/             # Biểu đồ PNG
└── data/processed/              # Bộ dữ liệu sau khi prepare thành công
```

Các thư mục đầu ra được tạo khi chạy. CSV đầu vào cần được cung cấp trước, chương trình không tự tải dữ liệu.

## Cách sử dụng

### Chuẩn bị môi trường

Mở Terminal PowerShell trong VS Code, kích hoạt môi trường hiện có và chuyển vào thư mục dự án:

```powershell
conda activate aqi_forecast
cd C:\MCU\ESP32\ESP32\ESP32_node\HN_PM25_forecasting
```

Các thư viện phục vụ pipeline là NumPy, pandas, Matplotlib, scikit-learn và PyYAML. Nếu môi trường chưa có, cài bằng:

```powershell
python -m pip install numpy pandas matplotlib scikit-learn PyYAML
```

Nếu PowerShell chưa nhận lệnh `conda`, mở Anaconda Prompt để kích hoạt môi trường, hoặc gọi trực tiếp Python của môi trường trên máy hiện tại:

```powershell
& "C:\Users\Lap4all\miniconda3\envs\aqi_forecast\python.exe" -m scripts.prepare_data --config configs/common.yaml --mode audit
```

### Chuẩn bị dữ liệu đầu vào và cấu hình

Đặt CSV gốc tại:

```text
data/Hanoi_aqi/hanoi_aqi_ml_ready_fixed.csv
```

Nếu CSV ở vị trí khác, sửa `paths.input_csv` trong `configs/common.yaml`. Các đường dẫn trong YAML được phân giải tương đối với thư mục gốc `HN_PM25_forecasting` khi dùng cấu hình này.

CSV cần có các cột:

| Cột | Nội dung |
| --- | --- |
| `datetime` hoặc `timestamp` | Mốc thời gian tròn giờ, ưu tiên `datetime` nếu có cả hai |
| `pm25` | Nồng độ PM2.5, đơn vị µg/m³ |
| `temperature` | Nhiệt độ, đơn vị °C |
| `humidity` | Độ ẩm tương đối, đơn vị % |
| `source` | Nhãn nguồn quan sát, cấu hình hiện tại chọn `OpenAQ` |

Không sửa CSV gốc để lấp chỗ thiếu. Pipeline xử lý khoảng thiếu theo chính sách đã cấu hình.

Các thiết lập chính trong `configs/common.yaml`:

| Thiết lập | Giá trị hiện tại | Ý nghĩa |
| --- | --- | --- |
| `data.lookback` | `24` | Số giờ lịch sử đầu vào |
| `data.horizon` | `24` | Số giờ PM2.5 cần dự báo |
| `data.features` | `[pm25, temperature, humidity]` | Thứ tự biến đầu vào cố định |
| `data.end_exclusive` | `"2025-11-22T00:00:00Z"` | Chỉ lấy mốc trước thời điểm này |
| `missing.method` | `linear` | Nội suy tuyến tính |
| `missing.max_gap_hours` | `3` | Độ dài khoảng thiếu tối đa được nội suy, `0` để tắt |
| `split.train_fraction` | `0.70` | Tỷ lệ miền thời gian train |
| `split.validation_fraction` | `0.15` | Tỷ lệ miền thời gian validation, phần còn lại là test |

Hợp đồng hiện tại cố định UTC, lịch sử 24 giờ, dự báo 24 giờ và thứ tự ba biến. Thay riêng các giá trị này trong YAML sẽ bị từ chối. Có thể thêm `data.start` để giới hạn mốc đầu, mốc đầu được lấy và mốc cuối không được lấy. Mốc lọc phải là chuỗi ISO có múi giờ, tròn giờ khi đổi về UTC.

### Kiểm tra dữ liệu và vẽ biểu đồ

Chạy từ thư mục `HN_PM25_forecasting`:

```powershell
python -m scripts.prepare_data --config configs/common.yaml --mode audit
```

Chế độ `audit` tạo báo cáo và biểu đồ, cho phép kiểm tra dữ liệu khi nguồn chưa được xác minh. Có thể chạy lại để cập nhật đầu ra sau khi đổi cấu hình hoặc màu biểu đồ.

| Đầu ra | Nội dung |
| --- | --- |
| `reports/tables/data_audit_summary.json` | Tổng hợp kiểm tra, ranh giới chia tập và số cửa sổ ứng viên |
| `reports/tables/data_*.csv` | Chi tiết dữ liệu thiếu, khoảng trống, độ bao phủ và mẫu bị loại |
| `reports/figures/data_series.png` | Chuỗi PM2.5, nhiệt độ, độ ẩm theo từng tập |
| `reports/figures/data_missingness.png` | Vị trí quan sát bị thiếu |
| `reports/figures/data_gaps.png` | Phân bố độ dài các khoảng giờ thiếu |
| `reports/figures/data_train_distribution.png` | Phân bố giá trị thuộc miền train |
| `reports/figures/data_source_series.png` | Quan sát PM2.5 theo nguồn |
| `reports/figures/data_coverage.png` | So sánh số cửa sổ khi nội suy và khi không điền |
| `reports/figures/data_feature_missing_histogram.png` | Phân bố khoảng thiếu theo từng biến |

Trong `data_series.png`, **train màu xanh lá, val màu cam, test màu đỏ**. Màu được đặt trong `src/plots.py`, mã màu hex phải có dấu `#`, ví dụ `#A8D5BA`.

Thông báo `provenance_unverified` cho biết nguồn chưa được xác minh. Các số liệu cửa sổ lúc này chỉ là chẩn đoán từ nguồn ứng viên, chưa phải bộ dữ liệu sẵn sàng huấn luyện. Nếu audit thất bại, các biểu đồ và bảng từ lần chạy trước có thể đã cũ.

### Tạo bộ dữ liệu cho huấn luyện

Trước khi chạy `prepare`, rà soát bằng chứng về trạm đo, nguồn, đơn vị, múi giờ, xử lý trước đó và thời điểm quan sát có thể sử dụng. Khai báo thông tin đã xác minh trong `provenance.evidence` với các trường `document`, `station`, `units`, `timezone`, `processing`, `availability`, `sources`. `document` phải trỏ đến tài liệu tồn tại, `units` là `ug/m3` hoặc `µg/m³`, `timezone` là `UTC`, `sources` là danh sách bao phủ các nguồn được cho phép.

Cấu hình hiện tại có `evidence: {}`, vì vậy **`prepare` sẽ từ chối tạo bộ dữ liệu**. Điền thông tin cấu trúc chỉ giúp chương trình kiểm tra điều kiện, tính xác thực của bằng chứng vẫn cần được rà soát.

Khi đã đủ bằng chứng và các tập có cửa sổ hợp lệ, chạy:

```powershell
python -m scripts.prepare_data --config configs/common.yaml --mode prepare
```

Kết quả được lưu tại `paths.processed`, hiện là `data/processed/pm25_24x24_20251121_linear_v2/`:

- `train.npz`, `validation.npz`, `test.npz`, chứa `X` dạng `(N, 24, 3)`, `y` dạng `(N, 24)`, nhãn gốc và metadata thời gian, nội suy.
- `scalers.json`, chứa tham số chuẩn hóa đã khớp trên train.
- `window_metadata.csv`, chứa thông tin từng cửa sổ.
- `manifest.json`, ghi nguồn, cấu hình, cách chia tập, số mẫu và phiên bản bộ dữ liệu.

`N` là số mẫu hợp lệ của từng tập. Các mô hình sau này cần sử dụng chung bộ dữ liệu và scaler này để so sánh nhất quán. Nếu thư mục `paths.processed` đã tồn tại, chọn một đường dẫn phiên bản mới trong cấu hình, chương trình không ghi đè bộ dữ liệu đã tạo.

### Khám phá bằng notebook

Nếu cần xem tương tác, cài JupyterLab và kernel trong cùng môi trường:

```powershell
python -m pip install jupyterlab ipykernel
python -m jupyterlab notebooks/01_explore_data.ipynb
```

Chọn kernel sử dụng môi trường `aqi_forecast`. Dữ liệu đầu vào và các điều kiện xác minh vẫn áp dụng như khi chạy từ terminal.
