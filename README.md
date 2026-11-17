
```markdown
# 🔐 SmartLock IoT — Hệ Thống Khóa Cửa Thông Minh Đa Phương Thức (STM32F103C8T6)

![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6%20(Blue%20Pill)-blue.svg)
![IDE](https://img.shields.io/badge/IDE-STM32CubeIDE%201.7.0-brightgreen.svg)
![Language](https://img.shields.io/badge/Language-C%20(HAL%20Driver)-orange.svg)
![IoT](https://img.shields.io/badge/IoT-ESP8266%20%2F%20ESP32%20(UART)-red.svg)

Dự án thiết kế và lập trình hệ thống khóa cửa an ninh thông minh tích hợp IoT trên nền tảng vi điều khiển **ARM Cortex-M3 (STM32F103C8T6)**. Hệ thống hỗ trợ mở khóa đa phương thức (Thẻ từ RFID, Mật khẩu PIN, Điều khiển từ xa qua Internet), tích hợp cảnh báo đột nhập bằng cảm biến chuyển động PIR, khóa chống dò mật khẩu và đồng bộ nhật ký hoạt động lên Cloud Server (ThingSpeak).

---

## ✨ Tính Năng Chính

* **Mở khóa đa phương thức:**
  * **Thẻ từ RFID (13.56MHz - RC522):** Nhận diện UID thẻ hợp lệ và mở chốt cửa tức thì.
  * **Bàn phím ma trận 4x4:** Nhập mã PIN 4 chữ số (`1234` mặc định), hỗ trợ xóa ký tự nhập sai (`*`) và xác nhận (`#`).
  * **Điều khiển từ xa qua IoT (UART2):** Lắng nghe lệnh `OPEN` từ module WiFi (ESP-01S / ESP32) thông qua cơ chế ngắt nhận UART không đồng bộ (`HAL_UART_Receive_IT`).
* **Quản trị bảo mật cục bộ:**
  * **Tự động khóa:** Chốt Servo tự động đóng lại sau **5 giây** kể từ khi mở cửa thành công.
  * **Chống dò mật khẩu (Brute-force Protection):** Nhập sai mật khẩu hoặc quẹt thẻ lạ quá **3 lần** liên tiếp sẽ kích hoạt còi báo động, bật LED đỏ và khóa vô hiệu hóa bàn phím trong **60 giây**.
  * **Đổi mật khẩu trực tiếp (Phím `B`):** Yêu cầu xác thực mật khẩu cũ trước khi cập nhật mật khẩu mới.
  * **Quản lý thẻ từ bằng thẻ Master (Phím `C`):** Quẹt thẻ Master để thêm thẻ mới hoặc xóa thẻ cũ khỏi danh sách cấp phép (tối đa 5 thẻ).
* **Chế độ Vắng nhà & Chống trộm (Phím `A`):**
  * Kích hoạt cảm biến chuyển động hồng ngoại **PIR**. Nếu phát hiện có người di chuyển khi cửa đang khóa, hệ thống lập tức hú còi Buzzer, nháy đèn đỏ cảnh báo và đẩy gói tin báo động lên Server.
* **Giao diện trực quan & Ghi nhật ký (Logging):**
  * Hiển thị trạng thái hệ thống, chế độ hoạt động và hướng dẫn thao tác theo thời gian thực trên màn hình **OLED SSD1306 (128x64)**.
  * Gửi mã sự kiện đóng/mở cửa và cảnh báo an ninh lên **ThingSpeak** qua giao thức TCP/HTTP.

---

## 🛠️ Phần Cứng Sử Dụng

| STT | Linh kiện | Thông số / Giao tiếp | Vai trò trong hệ thống |
| :-: | :--- | :--- | :--- |
| 1 | **STM32F103C8T6 (Blue Pill)** | ARM Cortex-M3, 64MHz (HSI PLL) | Vi điều khiển trung tâm xử lý toàn bộ logic |
| 2 | **Màn hình OLED 0.96"** | Chip SSD1306, I2C (`0x3C`) | Hiển thị giao diện người dùng & trạng thái |
| 3 | **Module RFID MFRC522** | Giao tiếp SPI1 (13.56MHz) | Đọc mã UID thẻ từ / móc khóa RFID |
| 4 | **Bàn phím ma trận 4x4** | Quét ma trận GPIO (8 chân) | Nhập PIN và chọn các phím chức năng (`A`, `B`, `C`, `*`, `#`) |
| 5 | **Module WiFi ESP-01S / ESP32** | Giao tiếp UART2 (`115200 bps`) | Kết nối Internet, nhận lệnh mở cửa và gửi log |
| 6 | **Động cơ Servo SG90** | Điều khiển xung PWM (50Hz) | Cơ cấu chấp hành đóng/mở chốt cửa (`0°` / `90°`) |
| 7 | **Cảm biến chuyển động PIR** | HC-SR501 (Digital Input) | Phát hiện chuyển động đột nhập ở chế độ Vắng nhà |
| 8 | **Còi Buzzer & Đèn LED** | Digital Output (`3.3V`) | Phản hồi âm thanh phím bấm, báo động và trạng thái cửa |

---

## 🔌 Sơ Đồ Đấu Nối Phần Cứng (Pinout)

> **Lưu ý nguồn điện:** Các module **OLED**, **RC522**, **ESP-01S** sử dụng nguồn **3.3V**; **Servo SG90** và **cảm biến PIR** sử dụng nguồn **5V**. Tất cả linh kiện bắt buộc phải **nối chung dây GND** với board STM32.

| Thiết bị | Chân trên thiết bị | Chân STM32F103C8T6 | Cấu hình ngoại vi |
| :--- | :--- | :--- | :--- |
| **OLED SSD1306** | `SCL` / `SDA` | `PB6` / `PB7` | `I2C1` (100 kHz) |
| **RFID RC522** | `SDA (CS)`<br>`SCK` / `MISO` / `MOSI`<br>`RST` | `PA4`<br>`PA5` / `PA6` / `PA7`<br>`PB0` | `SPI1` (Full-Duplex Master, Soft NSS) |
| **ESP-01S / ESP32** | `RX` / `TX` | `PA2` (`TX2`) / `PA3` (`RX2`) | `USART2` (115200 bps, Global Interrupt) |
| **Keypad 4x4** | Hàng `R1` – `R4`<br>Cột `C1` – `C4` | `PB12`, `PB13`, `PB14`, `PB15`<br>`PA8`, `PA9`, `PA10`, `PA11` | `GPIO_Output` (Push-Pull)<br>`GPIO_Input` (Internal Pull-Up) |
| **Servo SG90** | `PWM` (Dây tín hiệu) | `PA0` | `TIM2_CH1` (Prescaler `63`, Period `19999`) |
| **Cảm biến PIR** | `OUT` | `PB1` | `GPIO_Input` (Internal Pull-Down) |
| **LED Xanh (Trạng thái)** | `Anode / On-board` | `PC13` | `GPIO_Output` (Active Low - Tích cực mức thấp) |
| **LED Đỏ (Cảnh báo)** | `Anode` (qua trở 330Ω) | `PB8` | `GPIO_Output` (Active High) |
| **Còi Buzzer** | Chân dương (`+`) | `PB9` | `GPIO_Output` (Active High) |

---

## 📂 Cấu Trúc Mã Nguồn

Dự án được tổ chức theo kiến trúc module hóa giúp tách biệt tầng điều khiển phần cứng (HAL Drivers) và tầng xử lý logic ứng dụng:

```text
IOTSecurity/
├── Core/
│   ├── Inc/
│   │   ├── main.h            # Định nghĩa chân và prototype hệ thống
│   │   ├── ssd1306.h         # Thư viện giao tiếp màn hình OLED I2C
│   │   ├── rc522.h           # Thư viện giao tiếp đầu đọc thẻ RFID SPI
│   │   ├── keypad.h          # Driver quét bàn phím ma trận 4x4
│   │   ├── esp8266.h         # Cấu hình WiFi, API Key và giao tiếp UART
│   │   └── door_hw.h         # Driver điều khiển Servo PWM, Buzzer và UI OLED
│   └── Src/
│       ├── main.c            # Máy trạng thái (State Machine) và luồng xử lý chính
│       ├── ssd1306.c         # Xử lý hiển thị ký tự và bộ đệm khung hình OLED
│       ├── rc522.c           # Xử lý giao thức thẻ MIFARE và đọc UID 4-byte
│       ├── keypad.c          # Thuật toán quét hàng/cột và chống dội phím (Debounce)
│       ├── esp8266.c         # Gửi tập lệnh AT, đẩy log HTTP và ngắt nhận lệnh từ xa
│       ├── door_hw.c         # Điều khiển góc quay Servo và cập nhật giao diện màn hình
│       ├── stm32f1xx_it.c    # Trình phục vụ ngắt hệ thống (USART2_IRQHandler)
│       └── stm32f1xx_hal_msp.c # Khởi tạo cấp thấp ngoại vi (Clock, GPIO, NVIC)
└── IOT_Security.ioc          # File cấu hình phần cứng STM32CubeMX

```

---

## 🚀 Hướng Dẫn Cài Đặt & Sử Dụng

### 1. Cấu hình thông số mạng & thẻ từ

Trước khi biên dịch, mở các file sau để điền thông số thực tế của bạn:

* Trong **`Core/Inc/esp8266.h`**:
```c
#define WIFI_SSID     "Ten_WiFi_Cua_Ban"
#define WIFI_PASS     "Mat_Khau_WiFi"
#define SERVER_IP     "api.thingspeak.com"
#define API_KEY       "MA_WRITE_API_KEY_THINGSPEAK"

```


* Trong **`Core/Src/main.c`**: Cập nhật 4 byte mã UID của thẻ từ trắng đi kèm bộ RC522 vào mảng `master_uid` và `valid_cards[0]`.

### 2. Biên dịch và Nạp chương trình

1. Clone repository về máy:
```bash
git clone [https://github.com/Quantryhard/IOTSecurity.git](https://github.com/Quantryhard/IOTSecurity.git)

```


2. Mở **STM32CubeIDE**, chọn **File $\rightarrow$ Open Projects from File System...** và trỏ tới thư mục vừa clone.
3. Nhấn **`Ctrl + B`** (Build Project) để biên dịch ra file thực thi `IOT_Security.elf` / `.hex`.
4. Kết nối mạch nạp **ST-Link V2** với board Blue Pill (đảm bảo 2 jumper `BOOT0` và `BOOT1` đang ở vị trí `0`), nhấn **Run (`Ctrl + F11`)** để nạp xuống vi điều khiển.

### 3. Bảng Phím Chức Năng Nhanh

| Phím | Chức năng | Mô tả thao tác |
| --- | --- | --- |
| **`0` – `9**` | Nhập mã PIN | Nhập mật khẩu gồm 4 chữ số (Mặc định: `1234`) |
| **`#`** | Xác nhận | Xác nhận mở cửa hoặc lưu mật khẩu mới |
| **`*`** | Hủy / Xóa | Xóa mật khẩu đang nhập hoặc thoát về màn hình chính |
| **`A`** | Chế độ Vắng nhà | Bật / Tắt giám sát đột nhập bằng cảm biến chuyển động PIR |
| **`B`** | Đổi mật khẩu | Nhập MK cũ $\rightarrow$ `#` $\rightarrow$ Nhập MK mới (4 số) $\rightarrow$ `#` |
| **`C`** | Quản lý thẻ RFID | Quẹt thẻ Master $\rightarrow$ Quẹt thẻ phụ để Thêm hoặc Xóa thẻ |

---

## 👨‍💻 Tác Giả

* **Lường Tú Quân**
* **Trần Bảo Long**
* **Vũ Đặng Đức Minh**
* GitHub: [@Quantryhard](https://www.google.com/search?q=https://github.com/Quantryhard)

```

```
