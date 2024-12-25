#include "WebInterface.h"

void WebInterface::begin() {
    server.on("/", [this]() { handleRoot(); });
    server.on("/api/status", [this]() { handleStatus(); });
    server.on("/api/logs", [this]() { handleLogs(); });
    server.on("/api/toggle", HTTP_POST, [this]() { handleToggle(); });
    server.on("/api/mqtt/save", HTTP_POST, [this]() { handleMQTTSave(); });
    server.on("/api/mqtt/reconnect", HTTP_POST, [this]() { handleMQTTReconnect(); });
    server.begin();
    addLog("Web服务器已启动");
}

void WebInterface::handle() {
    server.handleClient();
}

void WebInterface::addLog(const String& message) {
    if (logs.size() >= MAX_LOGS) {
        logs.erase(logs.begin());
    }
    logs.push_back({getTimestamp(), message});
}

String WebInterface::getTimestamp() {
    return String(millis()/1000) + "s";
}

void WebInterface::setMQTTStatus(bool connected) {
    mqttConnected = connected;
}

String WebInterface::getHTML() {
    String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>壁挂炉控制</title>"
                   "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                   "<style>"
                   "body{margin:20px auto;max-width:800px;font-family:Arial;background:#f5f5f5}"
                   ".card{background:#fff;padding:20px;margin:15px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
                   ".btn{padding:8px 20px;border:none;border-radius:4px;background:#007bff;color:white;cursor:pointer;transition:all 0.3s}"
                   ".btn:hover{background:#0056b3}"
                   ".btn:disabled{background:#28a745;cursor:default;opacity:0.8}"
                   ".status-row{display:flex;justify-content:space-between;align-items:center;margin:10px 0}"
                   ".status-label{font-weight:bold}"
                   "input{width:100%;padding:8px;margin:5px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
                   ".form-row{margin:10px 0}"
                   ".btn-container{text-align:right;margin-top:15px}"
                   "h1,h2{color:#333;margin-top:0}"
                   "</style></head>"
                   "<body>");
    
    // 状态卡片
    html += F("<div class='card'>"
              "<h2>设备状态</h2>"
              "<div class='status-row'>"
              "<span class='status-label'>继电器状态:</span>"
              "<span id='relayStatus'>--</span>"
              "</div>"
              "<div class='status-row'>"
              "<span class='status-label'>MQTT状态:</span>"
              "<span id='mqttStatus'>--</span>"
              "</div>"
              "<div class='status-row' id='mqttError' style='color:red;display:none'>"
              "<span class='status-label'>错误原因:</span>"
              "<span id='mqttErrorMsg'></span>"
              "</div>"
              "<div class='btn-container'>"
              "<button class='btn' onclick='toggleRelay(event)'>切换状态</button>"
              "</div>"
              "</div>");
    
    // MQTT配置卡片
    html += F("<div class='card'>"
              "<h2>MQTT配置</h2>"
              "<div class='status-row'>"
              "<span class='status-label'>服务器:</span>"
              "<span>") + String(*mqtt_server_ptr) + F("</span>"
              "</div>"
              "<div class='status-row'>"
              "<span class='status-label'>端口:</span>"
              "<span>") + String(*mqtt_port_ptr) + F("</span>"
              "</div>"
              "<div class='status-row'>"
              "<span class='status-label'>用户名:</span>"
              "<span>") + String(*mqtt_user_ptr) + F("</span>"
              "</div>"
              "<div class='btn-container'>"
              "<button id='reconnectBtn' class='btn' onclick='reconnectMQTT()' style='margin-right:10px'></button>"
              "<button class='btn' onclick='showMQTTConfig()'>修改配置</button>"
              "</div>"
              "</div>");
    
    // 日志卡片
    html += F("<div class='card'>"
              "<h2>运行日志</h2>"
              "<pre id='logs' style='margin:0;white-space:pre-wrap;word-wrap:break-word'></pre>"
              "</div>");
    
    // 添加配置对话框
    html += F("<div id='mqttConfigDialog' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.5);'>"
              "<div style='background:white;margin:20px auto;padding:20px;max-width:600px;border-radius:8px;'>"
              "<h2>修改MQTT配置</h2>"
              "<form id='mqttForm'>"
              "<div class='form-row'>"
              "<div class='status-label'>服务器:</div>"
              "<input type='text' name='server' required value='") + String(*mqtt_server_ptr) + F("'>"
              "</div>"
              "<div class='form-row'>"
              "<div class='status-label'>用户名:</div>"
              "<input type='text' name='user' value='") + String(*mqtt_user_ptr) + F("'>"
              "</div>"
              "<div class='form-row'>"
              "<div class='status-label'>密码:</div>"
              "<input type='password' name='pass' value=''>"
              "</div>"
              "<div class='btn-container'>"
              "<button type='button' class='btn' onclick='hideMQTTConfig()' style='background:#6c757d;margin-right:10px'>取消</button>"
              "<button type='submit' class='btn'>保存</button>"
              "</div>"
              "</form>"
              "</div>"
              "</div>");
    
    // JavaScript代码
    html += F("<script>"
              "function updateStatus() {"
              "  fetch('/api/status').then(r=>r.json()).then(data=>{"
              "    document.getElementById('relayStatus').textContent = data.relay ? '开启' : '关闭';"
              "    document.getElementById('mqttStatus').textContent = data.mqtt ? '已连接' : '未连接';"
              "    const reconnectBtn = document.getElementById('reconnectBtn');"
              "    if(data.mqtt) {"
              "      reconnectBtn.textContent = '已连接';"
              "      reconnectBtn.disabled = true;"
              "      reconnectBtn.style.background = '#28a745';"
              "    } else {"
              "      reconnectBtn.textContent = '重新连接';"
              "      reconnectBtn.disabled = false;"
              "      reconnectBtn.style.background = '#007bff';"
              "    }"
              "    if(data.mqttError) {"
              "      document.getElementById('mqttError').style.display = 'flex';"
              "      document.getElementById('mqttErrorMsg').textContent = data.mqttError;"
              "    } else {"
              "      document.getElementById('mqttError').style.display = 'none';"
              "    }"
              "  });"
              "}"
              
              "function updateLogs() {"
              "  fetch('/api/logs').then(r=>r.json()).then(data=>{"
              "    const logs = data.reverse().map(log => `${log.time}: ${log.msg}`).join('\\n');"
              "    document.getElementById('logs').textContent = logs;"
              "  });"
              "}"
              
              "function toggleRelay(event) {"
              "  const btn = event.target;"
              "  btn.disabled = true;"
              "  fetch('/api/toggle', {method:'POST'})"
              "    .then(updateStatus)"
              "    .finally(() => btn.disabled = false);"
              "}"
              
              "document.getElementById('mqttForm').onsubmit = function(e) {"
              "  e.preventDefault();"
              "  const formData = new FormData(e.target);"
              "  const data = Object.fromEntries(formData);"
              "  fetch('/api/mqtt/save', {"
              "    method: 'POST',"
              "    headers: {'Content-Type': 'application/json'},"
              "    body: JSON.stringify(data)"
              "  })"
              "  .then(r => r.json())"
              "  .then(data => {"
              "    alert(data.message);"
              "    hideMQTTConfig();"  // 关闭对话框
              "    location.reload();"  // 刷新页面以更新显示
              "  });"
              "};"
              
              "setInterval(updateStatus, 1000);"
              "setInterval(updateLogs, 5000);"
              "updateStatus(); updateLogs();"
              
              "function showMQTTConfig() {"
              "  document.getElementById('mqttConfigDialog').style.display = 'block';"
              "}"
              
              "function hideMQTTConfig() {"
              "  document.getElementById('mqttConfigDialog').style.display = 'none';"
              "}"
              
              "function reconnectMQTT() {"
              "  const btn = event.target;"
              "  btn.disabled = true;"  // 暂时禁用按钮
              "  fetch('/api/mqtt/reconnect', {method:'POST'})"
              "    .then(r => r.json())"
              "    .then(data => {"
              "      alert(data.message);"
              "      updateStatus();"
              "    })"
              "    .catch(err => alert('重连请求失败'))"
              "    .finally(() => btn.disabled = false);"  // 恢复按钮状态
              "}"
              "</script></body></html>");
    
    return html;
}

void WebInterface::handleRoot() {
    server.send(200, "text/html", getHTML());
}

void WebInterface::handleStatus() {
    String json = "{\"relay\":" + String(digitalRead(relayPin) == LOW) + 
                 ",\"mqtt\":" + String(mqttConnected);
    if (mqttErrorMsg.length() > 0) {
        json += ",\"mqttError\":\"" + mqttErrorMsg + "\"";
    }
    json += "}";
    server.send(200, "application/json", json);
}

void WebInterface::handleLogs() {
    String json = "[";
    for (const auto& log : logs) {
        if (json != "[") json += ",";
        json += "{\"time\":\"" + log.timestamp + "\",\"msg\":\"" + log.message + "\"}";
    }
    json += "]";
    server.send(200, "application/json", json);
}

void WebInterface::handleToggle() {
    int currentState = digitalRead(relayPin);
    digitalWrite(relayPin, !currentState);
    addLog(String("手动") + (currentState == LOW ? "关闭" : "开启") + "加热");
    
    // Publish the new state to MQTT
    String newState = (currentState == LOW) ? "OFF" : "ON";
    bool publishResult = client.publish("heater/state", newState.c_str());
    if (publishResult) {
        Serial.println("Message published successfully.");
    } else {
        Serial.println("Failed to publish message.");
    }
    
    server.send(200);
}

void WebInterface::handleMQTTSave() {
    if (server.hasArg("plain")) {
        String json = server.arg("plain");
        if (mqttConfigCallback) {
            // 解析所有MQTT配置参数
            String server = json.substring(json.indexOf("server\":\"") + 9);
            server = server.substring(0, server.indexOf("\""));
            
            String user = json.substring(json.indexOf("user\":\"") + 7);
            user = user.substring(0, user.indexOf("\""));
            
            String pass = json.substring(json.indexOf("pass\":\"") + 7);
            pass = pass.substring(0, pass.indexOf("\""));
            
            // 直接更新指针指向的值
            *mqtt_server_ptr = server;
            *mqtt_user_ptr = user;
            
            // 调用回调函数（主要用于更新密码和触发重连）
            mqttConfigCallback(server, *mqtt_port_ptr, user, pass);
            addLog("MQTT配置已更新: " + server);  // 添加日志以便调试
        }
        this->server.send(200, "application/json", "{\"message\":\"配置已保存\"}");
    } else {
        this->server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    }
}

void WebInterface::handleMQTTReconnect() {
    if (mqttReconnectCallback) {
        mqttReconnectCallback();
        server.send(200, "application/json", "{\"message\":\"正在尝试重新连接MQTT...\"}");
    } else {
        server.send(400, "application/json", "{\"message\":\"重连功能未配置\"}");
    }
} 