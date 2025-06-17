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

// ��ȡ��ǰ�����ַ���
std::string getCurrentDate() {
	std::time_t now = std::time(nullptr);
	char buf[20];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&now));
	return std::string(buf);
}

// ��ȡ��ǰʱ���ַ���
std::string getCurrentTime() {
	std::time_t now = std::time(nullptr);
	char buf[20];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
	return std::string(buf);
}

// ����Ŀ¼
void createDirectories(const std::string& path) {
	std::string currentPath;
	for (size_t i = 0; i < path.size(); ++i) {
		currentPath += path[i];
		if (path[i] == '\\' || path[i] == '/' || i == path.size() - 1) {
			CreateDirectoryA(currentPath.c_str(), nullptr);
		}
	}
}

// ������־�ļ�·��
std::ofstream createLogFile(const std::string& rootPath, const std::string& logType) {
	std::string date = getCurrentDate();
	std::string logDir = rootPath + "\\" + date;
	createDirectories(logDir);
	std::string logFilePath = logDir + "\\" + logType + ".log";
	return std::ofstream(logFilePath, std::ios::app);
}

// ��ӡ��־������̨���ļ�
void logMessage(std::ofstream& logFile, const std::string& message, bool isError = false) {
	if (isError) {
		// ���ÿ���̨����Ϊ��ɫ
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
		std::cerr << message << std::endl;
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); // �ָ�Ĭ����ɫ
	}
	else {
		std::cout << message << std::endl;
	}

	logFile << message << std::endl;
}

// ��� MySQL ����ͬ��״̬�ĺ���
bool checkReplicationStatus(const std::string& host, const int port, const std::string& user, const std::string& password, std::ofstream& logFile, std::ofstream& errorLogFile) {
	MYSQL* conn = mysql_init(nullptr);

	if (conn == nullptr) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] MySQL ��ʼ��ʧ�ܡ�", true);
		return false;
	}

	// ���ӵ� MySQL ���ݿ�
	if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(), nullptr, port, nullptr, 0)) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] �޷����ӵ� MySQL ������: " + host + " �û�: " + user + ". ������Ϣ: " + mysql_error(conn), true);
		mysql_close(conn);
		return false;
	}

	// ִ�� SHOW SLAVE STATUS ��ѯ
	if (mysql_query(conn, "SHOW SLAVE STATUS")) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] ִ�� SHOW SLAVE STATUS ��ѯʧ��: " + mysql_error(conn), true);
		mysql_close(conn);
		return false;
	}

	MYSQL_RES* result = mysql_store_result(conn);
	if (!result) {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] ��ȡ��ѯ���ʧ��: " + mysql_error(conn), true);
		mysql_close(conn);
		return false;
	}

	MYSQL_ROW row = mysql_fetch_row(result);
	bool isReplicationOk = true;

	if (row) {
		std::string ioState = row[10] ? row[10] : ""; // Slave_IO_Running
		std::string sqlState = row[11] ? row[11] : ""; // Slave_SQL_Running
		std::string secondsBehindMaster = row[32] ? row[32] : "δ֪"; // Seconds_Behind_Master

		if (ioState != "Yes" || sqlState != "Yes") {
			isReplicationOk = false;
			logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] ����ͬ��ʧ�ܡ�Slave_IO_Running: " + ioState + ", Slave_SQL_Running: " + sqlState + ", �ӳ�: " + secondsBehindMaster + " �롣", true);
		}
		else {
			logMessage(logFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [��Ϣ] ����ͬ���������ӳ�: " + secondsBehindMaster + " �롣");
		}
	}
	else {
		logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] δ�ҵ�����ͬ��״̬��Ϣ��", true);
		isReplicationOk = false;
	}

	// ������Դ
	mysql_free_result(result);
	mysql_close(conn);

	return isReplicationOk;
}

// ������
int main() {
	const std::string iniFile = ".\\config.ini";
	IniFile config(iniFile);

	const std::string host = config.get("Database", "Host", "127.0.0.1");
	const std::string port = config.get("Database", "Port", "3306");
	const std::string user = config.get("Database", "User", "root");
	const std::string password = config.get("Database", "Password");
	const std::string logRootPath = config.get("Settings", "LogRoot", ".\\logs");

	// ������־�ļ�
	std::ofstream logFile = createLogFile(logRootPath, "info");
	std::ofstream errorLogFile = createLogFile(logRootPath, "error");

	if (!logFile.is_open() || !errorLogFile.is_open()) {
		std::cerr << "[����] �޷�����־�ļ�����·��: " << logRootPath << std::endl;
		return 1;
	}

	// Debug �����ȡ���
	logMessage(logFile, "[����] ���ݿ�����: " + host + ", �˿�:" + port + ", �û�: " + user + ", ����: " + password);

	// ��������Ƿ�����
	if (host.empty() || user.empty() || password.empty()) {
		logMessage(errorLogFile, "[����] �����ļ���ȱ�����ݿ��������û���������Ϣ��", true);
		return 1;
	}

	int checkInterval;
	try {
		checkInterval = std::stoi(config.get("Settings", "CheckInterval", "60"));
	}
	catch (const std::exception& e) {
		logMessage(errorLogFile, "[����] �����ļ��� CheckInterval ֵ��Ч: " + std::string(e.what()) + ", ʹ��Ĭ��ֵ: 60 �롣", true);
		checkInterval = 60;
	}

	logMessage(logFile, "[��Ϣ] ��ʼ��� MySQL ����ͬ��״̬��ÿ " + std::to_string(checkInterval) + " ����һ�Ρ�");

	while (true) {
		logMessage(logFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [��Ϣ] ��ʼ��� MySQL ����ͬ��״̬...");

		if (!checkReplicationStatus(host, atoi(port.c_str()), user, password, logFile, errorLogFile)) {
			logMessage(errorLogFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [����] ����ͬ�����ʧ�ܣ�����������", true);
		}
		else {
			logMessage(logFile, "[" + getCurrentDate() + " " + getCurrentTime() + "] [��Ϣ] ����ͬ�����ͨ����");
		}

		// �ȴ�ָ�����ʱ��
		std::this_thread::sleep_for(std::chrono::seconds(checkInterval));
	}
	return 0;
}