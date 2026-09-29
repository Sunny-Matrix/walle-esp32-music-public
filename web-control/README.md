# WALI 设备控制网页

手机与 ESP32 接入同一个 Wi-Fi 后，在本目录启动静态网页：

```powershell
python -m http.server 8080
```

手机访问电脑局域网地址，例如 `http://电脑局域网IP:8080`，然后在“设置”里填写 ESP32 的 IP 地址。

网页用于控制瓦力的移动、头部、手臂和灯光，并显示设备连接状态。建议手机与电脑、ESP32 连接到同一个可信 Wi-Fi 网络。

网页接口：

- `GET /api/status`
- `POST /api/control/drive`，参数 `{ "left": 55, "right": 55 }`
- `POST /api/control/servo`，参数 `{ "name": "head", "value": 90 }`
- `POST /api/control/lights`，参数 `{ "on": true, "r": 242, "g": 106, "b": 46 }`
