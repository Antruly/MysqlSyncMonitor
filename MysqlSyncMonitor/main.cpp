#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <mysql.h>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <ctime>
#include <windows.h>
#include <map>

class IniFile {
private:
	std::string filePath;

public:
	IniFile(const std::string& path) : filePath(path) {}

	std::string get(const std::string& section, const std::string& key, const std::string& defaultValue = "") {
		char value[256];
		GetPrivateProfileStringA(section.c_str(), key.c_str(), defaultValue.c_str(), value, sizeof(value), filePath.c_str());
		return std::string(value);
	}

	void set(const std::string& section, const std::string& key, const std::string& value) {
		WritePrivateProfileStringA(section.c_str(), key.c_str(), value.c_str(), filePath.c_str());
	}

	void removeKey(const std::string& section, const std::string& key) {
		WritePrivateProfileStringA(section.c_str(), key.c_str(), nullptr, filePath.c_str());
	}

	void removeSection(const std::string& section) {
		WritePrivateProfileStringA(section.c_str(), nullptr, nullptr, filePath.c_str());
	}
};

// 获取当前日期字符串
std::string getCurrentDate() {
	std::time_t now = std::time(nullptr);
	char buf[20];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&now));
	return std::string(buf);
}

// 获取当前时间字符串
std::string getCurrentTime() {
	std::time_t now = std::time(nullptr);
	char buf[20];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
	return std::string(buf);
}

// 创建目录
void createDirectories(const std::string& path) {
	std::string currentPath;
	for (size_t i = 0; i < path.size(); ++i) {
		currentPath += path[i];
		if (path[i] == '\\' || path[i] == '/' || i == path.size() - 1) {
			CreateDirectoryA(currentPath.c_str(), nullptr);
		}
	}
}

// 创建日志文件路径
std::ofstream createLogFile(const std::string& rootPath, const std::string& logType) {
	std::string date = getCurrentDate();
	std::string logDir = rootPath + "\\" + date;
	createDirectories(logDir);
	std::string logFilePath = logDir + "\\" + logType + ".log";
	return std::ofstream(logFilePath, std::ios::app);
}

// 打印日志到控制台和文件
void logMessage(std::ofstream& logFile, const std::string& message, bool isError = false) {
	if (isError) {
		// 设置控制台字体为红色
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
		std::cerr << message << std::endl;
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); // 恢复默认颜色
	}
	else {
		std::cout << message << std::endl;
	}

	logFile << message << std::endl;
}

// 检查 MySQL 主备同步状态的函数
bool checkReplicationStatus(const std::string& host, const int port, const std::string& user, const std::string& password, std::ofstream& logFile, std::ofstream& errorLogFile) {
	MYSQL* conn = mysql_init(nullptr);

	if (conn == nullptr) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [错误] MySQL 初始化失败。", true);
		return false;
	}

	// 连接到 MySQL 数据库
	if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(), nullptr, port, nullptr, 0)) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [错误] 无法连接到 MySQL 服务器: " + host + " 用户: " + user + ". 错误信息: " + mysql_error(conn), true);
		mysql_close(conn);
		return false;
	}

	// 执行 SHOW SLAVE STATUS 查询
	if (mysql_query(conn, "SHOW SLAVE STATUS")) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [错误] 执行 SHOW SLAVE STATUS 查询失败: " + mysql_error(conn), true);
		mysql_close(conn);
		return false;
	}

	MYSQL_RES* result = mysql_store_result(conn);
	if (!result) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [错误] 获取查询结果失败: " + mysql_error(conn), true);
		mysql_close(conn);
		return false;
	}

	MYSQL_ROW row = mysql_fetch_row(result);
	bool isReplicationOk = true;

	if (row) {
		std::string ioState = row[10] ? row[10] : ""; // Slave_IO_Running
		std::string sqlState = row[11] ? row[11] : ""; // Slave_SQL_Running
		std::string secondsBehindMaster = row[32] ? row[32] : "未知"; // Seconds_Behind_Master

		if (ioState != "Yes" || sqlState != "Yes") {
			isReplicationOk = false;
			logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [警告] 主从同步失败。Slave_IO_Running: " + ioState + ", Slave_SQL_Running: " + sqlState + ", 延迟: " + secondsBehindMaster + " 秒。", true);
		}
		else {
			logMessage(logFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [信息] 主从同步正常，延迟: " + secondsBehindMaster + " 秒。");
		}
	}
	else {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [错误] 未找到主从同步状态信息。", true);
		isReplicationOk = false;
	}

	// 清理资源
	mysql_free_result(result);
	mysql_close(conn);

	return isReplicationOk;
}

// 主程序
int main() {
	const std::string iniFile = ".\\config.ini";
	IniFile config(iniFile);

	const std::string host = config.get("Database", "Host", "127.0.0.1");
	const std::string port = config.get("Database", "Port", "3306");
	const std::string user = config.get("Database", "User", "root");
	const std::string password = config.get("Database", "Password");
	const std::string logRootPath = config.get("Settings", "LogRoot", ".\\logs");

	// 创建日志文件
	std::ofstream logFile = createLogFile(logRootPath, "info");
	std::ofstream errorLogFile = createLogFile(logRootPath, "error");

	if (!logFile.is_open() || !errorLogFile.is_open()) {
		std::cerr << "[错误] 无法打开日志文件，根路径: " << logRootPath << std::endl;
		return 1;
	}

	// Debug 输出读取结果
	logMessage(logFile, "[调试] 数据库主机: " + host + ", 端口:" + port + ", 用户: " + user + ", 密码: " + password);

	// 检查配置是否完整
	if (host.empty() || user.empty() || password.empty()) {
		logMessage(errorLogFile, "[错误] 配置文件中缺少数据库主机、用户或密码信息。", true);
		return 1;
	}

	int checkInterval;
	try {
		checkInterval = std::stoi(config.get("Settings", "CheckInterval", "60"));
	}
	catch (const std::exception& e) {
		logMessage(errorLogFile, "[错误] 配置文件中 CheckInterval 值无效: " + std::string(e.what()) + ", 使用默认值: 60 秒。", true);
		checkInterval = 60;
	}

	logMessage(logFile, "[信息] 开始监控 MySQL 主从同步状态，每 " + std::to_string(checkInterval) + " 秒检查一次。");

	while (true) {
		logMessage(logFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [信息] 开始检查 MySQL 主从同步状态...");

		if (!checkReplicationStatus(host, atoi(port.c_str()), user, password, logFile, errorLogFile)) {
			logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [严重] 主从同步检查失败，请立即处理！", true);
		}
		else {
			logMessage(logFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [信息] 主从同步检查通过。");
		}

		// 等待指定间隔时间
		std::this_thread::sleep_for(std::chrono::seconds(checkInterval));
	}
	return 0;
}