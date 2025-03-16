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
#include <thread>

using namespace std;
using namespace std::chrono;

static int server_socket;  // Глобальный, чтобы можно было закрыть при Ctrl+C

// Обработчик SIGINT, чтобы корректно завершать сервер
void handle_signal(int signal) {
    if (signal == SIGINT) {
        cout << "\nЗавершение работы потребителя." << endl;
        close(server_socket);
        exit(0);
    }
}

// Функция, которая в цикле обрабатывает подключения
// и на каждого клиента создаёт поток handle_client.
void handle_client(int client_socket) {
    // В эту строку будем накапливать всё, что приходит с сокета
    string data;

    while (true) {
        // Порциями читаем данные от клиента
        char buffer[4096];
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));

        // Если ошибка чтения или клиент закрыл соединение
        if (bytes_read < 0) {
            cerr << "Ошибка при чтении запроса от клиента!" << endl;
            close(client_socket);
            break;
        }
        if (bytes_read == 0) {
            cout << "Клиент отключился." << endl;
            close(client_socket);
            break;
        }

        // Добавляем прочитанный кусок в общий буфер
        data.append(buffer, bytes_read);

        // Пробуем распарсить задание (N + 2*N*N чисел) из того, что уже накопилось
        // Для упрощения возьмём логику: мы ждем ровно ОДНО задание от клиента,
        // а если Producer пошлёт ещё, то здесь же обрабатываем (в том же while).
        // Но ниже приведён пример на 1 задание. Если нужно обрабатывать много подряд – используйте цикл.
        while (true) {
            // Создадим строковый поток из текущих накопленных данных
            istringstream iss(data);

            // Считаем N
            int N;
            if (!(iss >> N)) {
                // Если не смогли прочитать даже N, значит данных ещё не хватает.
                // Выходим из вложенного while, чтобы дочитать по сети.
                break;
            }

            // Пробуем считать 2*N*N чисел
            vector<vector<int>> A(N, vector<int>(N));
            vector<vector<int>> B(N, vector<int>(N));

            bool enoughData = true;  // флажок, что получилось считать все числа
            for (int i = 0; i < N && enoughData; i++) {
                for (int j = 0; j < N && enoughData; j++) {
                    if (!(iss >> A[i][j])) {
                        enoughData = false;
                    }
                }
            }
            for (int i = 0; i < N && enoughData; i++) {
                for (int j = 0; j < N && enoughData; j++) {
                    if (!(iss >> B[i][j])) {
                        enoughData = false;
                    }
                }
            }

            if (!enoughData) {
                // Значит, нам не хватило чисел для полной матрицы A и B
                // Остаёмся ждать, когда придут ещё данные
                break;
            }

            // Если мы здесь ― значит задание считано полностью.
            // Определим, сколько байт мы реально «извлекли» из строки:
            streampos pos = iss.tellg();
            if (pos < 0) {
                // Неожиданный сбой парсинга
                cerr << "Ошибка парсинга входных данных." << endl;
                close(client_socket);
                return;
            }

            // Удаляем из data уже обработанную часть (1 + 2*N*N чисел)
            data.erase(0, static_cast<size_t>(pos));

            // --- Выполняем умножение матриц ---
            vector<vector<int>> C(N, vector<int>(N, 0));
            auto start = high_resolution_clock::now();
            for (int i = 0; i < N; i++) {
                for (int j = 0; j < N; j++) {
                    for (int k = 0; k < N; k++) {
                        C[i][j] += A[i][k] * B[k][j];
                    }
                }
            }
            auto end = high_resolution_clock::now();
            auto duration = duration_cast<milliseconds>(end - start).count();

            // Сумма элементов результата
            long long sum = 0;
            for (int i = 0; i < N; i++) {
                for (int j = 0; j < N; j++) {
                    sum += C[i][j];
                }
            }

            // Формируем ответ
            string result = "СУММА=" + to_string(sum) + " ВРЕМЯ=" + to_string(duration) + "мс";
            ssize_t bytes_sent = send(client_socket, result.c_str(), result.size(), 0);
            if (bytes_sent < 0) {
                cerr << "Ошибка при отправке результата!" << endl;
            }

            cout << "Выполнено умножение матриц: размер " << N << "x" << N
                 << ", сумма элементов = " << sum << ", время вычисления = " << duration << "мс" << endl;

            // Если нужно обрабатывать следующие задания в том же соединении,
            // здесь можно не делать break, а продолжать парсить leftover, пока там есть полные задания.
            // Для примера ― останемся в этом while, чтобы попробовать парсить «следующее» задание,
            // если Producer отправит несколько подряд в этом же соединении.
        }
    }
}

// Инициализация слушающего сокета
void initialize_server_socket(int port) {
    struct sockaddr_in server_addr{};
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        cerr << "Ошибка при создании сокета!" << endl;
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Ошибка привязки сокета к порту!" << endl;
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 50) < 0) {
        cerr << "Ошибка перехода в режим прослушивания!" << endl;
        close(server_socket);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    // Отключаем буферизацию вывода для немедленного отображения сообщений
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc != 3 || string(argv[1]) != "-p") {
        cerr << "Использование: ./consumer -p <порт>" << endl;
        return EXIT_FAILURE;
    }
    int port = stoi(argv[2]);

    // Устанавливаем обработчик сигнала SIGINT (Ctrl+C)
    signal(SIGINT, handle_signal);

    // Инициализируем сервер
    initialize_server_socket(port);
    cout << "Потребитель запущен на порту " << port << "." << endl;

    // Принимаем клиентов в бесконечном цикле
    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            cerr << "Ошибка при принятии подключения клиента!" << endl;
            continue;
        }

        cout << "Клиент подключился: " << inet_ntoa(client_addr.sin_addr)
             << ":" << ntohs(client_addr.sin_port) << endl;

        // Запускаем поток для обслуживания нового клиента
        thread t(handle_client, client_fd);
        t.detach();
    }

    return 0;
}
