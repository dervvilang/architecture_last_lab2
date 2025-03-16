#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <netdb.h>

using namespace std;
using namespace std::chrono;

bool stop_producer = false;

// Обработчик сигнала (Ctrl + C)
void handle_signal(int signal) {
    if (signal == SIGINT) {
        cout << "\nПроизводитель завершает работу." << endl;
        stop_producer = true;
    }
}

// Устанавливаем соединение (создаём сокет и делаем connect)
int setup_connection(const string &server_host, int server_port, struct sockaddr_in &server_address) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        cerr << "Ошибка создания сокета!" << endl;
        return -1;
    }
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);

    // Разрешаем DNS-имя сервера
    struct hostent *he = gethostbyname(server_host.c_str());
    if (he == nullptr) {
        cerr << "Ошибка: не удалось разрешить имя хоста!" << endl;
        close(socket_fd);
        return -1;
    }
    memcpy(&server_address.sin_addr, he->h_addr_list[0], he->h_length);

    // Пытаемся сконнектиться
    if (connect(socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        cerr << "Сервер недоступен." << endl;
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

// Генерация тестовых матриц (случайные числа 0..9)
string generate_task(int matrix_size) {
    ostringstream oss;
    oss << matrix_size << " ";
    srand(time(NULL) + rand()); // чуть более разнообразный seed

    int total_elements = matrix_size * matrix_size;
    // Матрица A
    for (int i = 0; i < total_elements; i++) {
        int val = rand() % 10;
        oss << val << " ";
    }
    // Матрица B
    for (int i = 0; i < total_elements; i++) {
        int val = rand() % 10;
        oss << val << " ";
    }
    return oss.str();
}

int main(int argc, char *argv[]) {
    // Значения по умолчанию
    string server_host = "127.0.0.1";
    int server_port = 9090;
    int task_count = 10;
    bool run_infinite = false;
    int matrix_size = 10;

    // Пример: ./producer <server:port> [-n <кол-во_заданий>] [-i] [-m <размер>]
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if(arg == "-n" && i+1 < argc) {
            task_count = stoi(argv[++i]);
            cout << "Количество заданий установлено: " << task_count << endl;
        } else if(arg == "-i") {
            run_infinite = true;
            cout << "Включён бесконечный режим." << endl;
        } else if(arg == "-m" && i+1 < argc) {
            matrix_size = stoi(argv[++i]);
            cout << "Размер матрицы установлен: " << matrix_size << "x" << matrix_size << endl;
        } else if(arg.find(":") != string::npos) {
            // сервер:порт
            server_host = arg.substr(0, arg.find(":"));
            server_port = stoi(arg.substr(arg.find(":") + 1));
        } else {
            cerr << "Использование: ./producer <server:port> [-n <кол-во_заданий>] [-i] [-m <размер_матрицы>]" << endl;
            exit(EXIT_FAILURE);
        }
    }

    signal(SIGINT, handle_signal);

    auto overall_start = high_resolution_clock::now();
    int tasks_done = 0;

    while (!stop_producer && (run_infinite || tasks_done < task_count)) {
        // На КАЖДОЕ задание делаем новое соединение
        struct sockaddr_in server_address{};
        int socket_fd = setup_connection(server_host, server_port, server_address);
        if (socket_fd < 0) {
            cerr << "Не удалось подключиться к " << server_host << ":" << server_port
                 << ". Повтор через 3 секунды..." << endl;
            sleep(3);
            continue;
        }
        cout << "Подключено к " << server_host << ":" << server_port << endl;

        // Генерируем задание
        string task_msg = generate_task(matrix_size);

        // Отправляем задание
        ssize_t bytes_sent = send(socket_fd, task_msg.c_str(), task_msg.size(), 0);
        if (bytes_sent < 0) {
            cerr << "Ошибка отправки задания! Попробуем заново..." << endl;
            close(socket_fd);
            continue;
        }
        cout << "Отправлено задание: умножение матриц " << matrix_size << "x" << matrix_size << endl;

        // Читаем ответ
        const int BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        ssize_t bytes_received = read(socket_fd, buffer, BUFFER_SIZE);
        if (bytes_received > 0) {
            string result(buffer, bytes_received);
            cout << "Получен результат: " << result << endl;
        } else {
            cerr << "Ошибка при получении ответа от сервера!" << endl;
        }

        close(socket_fd);
        tasks_done++;
        usleep(200000); // 0.2 секунды
    }

    auto overall_end = high_resolution_clock::now();
    auto total_duration = duration_cast<milliseconds>(overall_end - overall_start).count();
    cout << "Общее время обработки: " << total_duration << " мс" << endl;

    return 0;
}
