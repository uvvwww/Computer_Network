# 端對端加密聊天與檔案傳輸系統

**課程：** 計算機網路 2025 - Socket Programming 專案 Phase 2  
**學號：** b12902153, b12902042  
**姓名：** 林廷穎、劉上綸

---

## 專案概述

本專案實作了一個安全的多執行緒聊天應用程式，採用 **Client-Server 架構**進行使用者探索，並使用 **P2P (Peer-to-Peer) 架構**進行直接通訊。

### 核心特色
- ✅ **端對端加密 (E2EE)**：使用 ECDH 金鑰交換 + AES-256-CBC 加密
- ✅ **P2P 檔案傳輸**：支援大型檔案分塊加密傳輸
- ✅ **群組聊天功能**：支援多人同時加密群組聊天（Relay 模式）
- ✅ **Thread Pool 伺服器**：高效能多執行緒架構，無 fork() 開銷
- ✅ **安全認證機制**：使用者註冊、登入、密碼驗證
- ✅ **Thread-Safe 設計**：使用 mutex 保護共享資源，防止競態條件

---

## 功能特性

### 1. 伺服器端 (Server)

#### 🔹 Thread Pool 架構
- 使用執行緒池 (Thread Pool) 處理並行連線，取代傳統的 fork() 模式
- 動態任務分配，有效管理系統資源

#### 🔹 使用者管理
- **註冊 (Register)**：建立使用者帳號與密碼
- **登入 (Login)**：驗證身份並維護線上狀態
- **登出 (Logout)**：安全斷線並更新使用者狀態
- **防重複登入**：偵測並拒絕重複登入的請求

#### 🔹 群組聊天 (Group Chat)
- **Relay 模式**：伺服器作為中繼站，轉送加密訊息給所有群組成員
- **動態成員管理**：使用 `std::set` 追蹤群組成員，支援隨時加入/離開
- **通知機制**：成員加入或離開時，自動通知其他群組成員

#### 🔹 執行緒安全
- 使用 `std::mutex` 保護全域資源（使用者列表、請求表、群組成員集合）
- 避免 Race Condition 與資料不一致問題

---

### 2. 客戶端 (Client)

#### 🔹 P2P 通訊架構
- 透過伺服器取得對方的 IP 與 Port 後，直接建立點對點連線
- 聊天與檔案傳輸皆在 P2P 連線上進行，伺服器不經手內容

#### 🔹 端對端加密 (E2EE)
- **金鑰交換**：使用 **Elliptic Curve Diffie-Hellman (ECDH)**（prime256v1 曲線）安全地產生共享金鑰
- **加密演算法**：採用 **AES-256-CBC** 模式，使用 OpenSSL 加密所有訊息與檔案內容
- **防竊聽**：即使通訊被攔截，未經授權者也無法解密內容

#### 🔹 P2P 檔案傳輸
- 支援二進位安全傳輸（Binary-Safe）
- 大型檔案自動分塊、加密後傳送，接收端重組還原
- 檔案傳輸格式：`__FILE_START__|檔名|檔案大小` + 加密檔案內容

#### 🔹 聊天請求機制
- 發起方送出請求 (`REQ <Username>`)
- 接收方必須明確接受 (`y`) 或拒絕 (`n`)
- 接受後雙方交換 ECDH 公鑰，建立加密通道

---

### 3. 穩健性與異常處理

- ✅ **防重複登入**：伺服器自動偵測並拒絕已登入帳號的重複連線
- ✅ **輸入驗證**：檢查 Port 範圍、使用者名稱合法性
- ✅ **安全 I/O**：處理 `EINTR` 訊號中斷與部分讀寫 (Partial Read/Write)
- ✅ **錯誤處理**：使用 OpenSSL 錯誤處理函式，確保加密操作正確性

---

## 目錄結構

```
.
├── Makefile            # 建置腳本（支援 macOS / Linux）
├── README.md           # 專案文件（本檔案）
├── server.cpp          # 伺服器主程式
├── server.hpp          # 伺服器資料結構定義
├── client.cpp          # 客戶端主程式
├── client.hpp          # 客戶端設定
├── ThreadPool.cpp      # Thread Pool 實作
├── ThreadPool.hpp      # Thread Pool 標頭檔
├── crypto_utils.hpp    # OpenSSL 加密工具（AES, ECDH）
├── io_utils.cpp        # 安全 I/O 實作
├── io_utils.hpp        # 安全 I/O 標頭檔
└── conn_utils.hpp      # 連線輔助函式
```

---

## 編譯與執行

### 環境需求
- **作業系統**：macOS (Apple Silicon) 或 Linux
- **編譯器**：g++ (支援 C++17)
- **函式庫**：OpenSSL 3.0、pthread

### 步驟 1：安裝相依套件

#### macOS (使用 Homebrew)
```bash
brew install openssl@3
```

#### Ubuntu/Debian
```bash
sudo apt-get install libssl-dev build-essential
```

### 步驟 2：產生 SSL 憑證（可選）
專案可使用 SSL/TLS 進行初始連線（目前實作為選用功能）。

```bash
make cert
```

### 步驟 3：編譯專案
```bash
make
```
成功後會產生 `server` 和 `client` 兩個執行檔。

### 步驟 4：清理編譯檔案
```bash
make clean
```

---

## 使用說明

### 步驟 1：啟動伺服器
伺服器需指定監聽 Port。

```bash
./server 8888
```

伺服器會顯示：
```
Server listening on port 8888
```

---

### 步驟 2：啟動客戶端
開啟多個終端機視窗，分別執行客戶端程式。

```bash
./client
```

依照提示輸入連線資訊：
- **伺服器位址**：`127.0.0.1`（或 `localhost`）
- **伺服器 Port**：`8888`

---

### 步驟 3：使用者操作指令

#### 📌 使用者管理

##### 註冊
```
register
```
依照提示輸入使用者名稱與密碼。

##### 登入
```
login
```
依照提示輸入：
1. 使用者名稱
2. 密碼
3. 監聽 Port（用於接收 P2P 連線）

##### 登出
```
logout
```
安全登出系統。

##### 列出線上使用者
```
list
```
顯示目前所有線上的使用者。

---

#### 📌 P2P 聊天與檔案傳輸

##### 發起聊天
```
chat <使用者名稱>
```
範例：
```
chat Bob
```

##### 接收聊天請求
當有人向你發起聊天請求時，畫面會顯示：
```
*** Incoming Chat Request from Alice ***
Do you want to accept? (Type 'y' to accept, 'n' to reject):
```
- 輸入 `y` 接受，建立加密通道
- 輸入 `n` 拒絕

##### 聊天室內操作

**傳送訊息**  
直接輸入文字後按 Enter。

**傳送檔案**  
```
sendfile <檔案路徑>
```
範例：
```
sendfile photos/cat.jpg
```
對方會自動接收並儲存為 `recv_<檔名>`。

**離開聊天**  
```
exit
```

---

#### 📌 群組聊天 (Group Chat)

##### 加入群組
```
group-chat
```
進入群組聊天模式，所有線上使用者都可以看到你的訊息。

##### 群組聊天特色
- **加密傳輸**：所有群組訊息使用預設群組金鑰加密（AES-256-CBC）
- **即時廣播**：伺服器即時轉送訊息給所有群組成員
- **成員通知**：當成員加入或離開時，會顯示系統通知
- **訊息格式**：`[Group] Username: 訊息內容`

##### 傳送群組訊息
直接輸入文字後按 Enter，訊息會廣播給所有群組成員。

##### 離開群組
```
exit
```
離開群組後回到主選單。

---

## 技術細節

### 通訊協定設計

#### 🔹 聊天請求握手 (Handshake)
1. **發起方**傳送：`REQ <Username>`
2. **接收方**回應：
   - 接受：`ACC <Username> <PublicKey>`（附帶 ECDH 公鑰）
   - 拒絕：`REJECT`
3. **發起方**傳送自己的公鑰，雙方計算共享金鑰

#### 🔹 檔案傳輸格式
- **Header**（加密）：`__FILE_START__|檔名|檔案大小`
- **Body**（加密）：分塊二進位資料

#### 🔹 加密流程
1. 雙方各自產生 ECDH Key Pair
2. 交換 Public Key
3. 計算共享金鑰 (Shared Secret)
4. 使用 AES-256-CBC 加密通訊內容

#### 🔹 群組聊天協定
- **架構**：伺服器 Relay 模式，非 P2P
- **加密方式**：使用固定的群組金鑰（由 `get_group_key()` 產生）
- **訊息格式**：`[Group] Username: <HexEncodedEncryptedPayload>`
- **轉送機制**：
  1. 客戶端加密訊息並轉成 Hex String
  2. 伺服器收到後直接轉送給其他群組成員（不解密）
  3. 接收端從 Hex String 還原並解密
- **成員管理**：伺服器使用 `std::set<int>` 追蹤群組 FD，使用 mutex 保護

---

## 已知限制與未來改進

- [ ] 群組聊天支援 P2P 多播 (Multicast) 模式
- [ ] 實作訊息重送機制
- [ ] 加入訊息簽章驗證 (Message Authentication)
- [ ] 優化大型檔案的傳輸效率
- [ ] 群組聊天支援動態金鑰協商

---

## 參考資料

- [OpenSSL Documentation](https://www.openssl.org/docs/)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [ECDH Key Exchange](https://en.wikipedia.org/wiki/Elliptic-curve_Diffie%E2%80%93Hellman)

---

## 授權聲明

本專案僅供學術用途，請勿用於商業目的。