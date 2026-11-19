
 // =====================================================================
 //  server.cpp — ШАБЛОН TCP-СЕРВЕРА (зачётный проект)
 //
 //  Что делает: ждёт подключения клиента на порту 9000, принимает от него
 //  команды (по одной строке) и на каждую команду отправляет одну строку
 //  ответа. Когда клиент отключился, сервер ждёт следующего клиента.
 //
 //  Наш ПРОТОКОЛ (договорённость между клиентом и сервером):
 //    1. Сразу после подключения сервер присылает строку-приветствие.
 //    2. Клиент присылает команду — одну строку, в конце символ '\n'.
 //    3. Сервер отвечает ровно ОДНОЙ строкой, в конце тоже '\n'.
 //    4. Команда QUIT: сервер отвечает "BYE" и закрывает соединение.
 //
 //  Где писать свой код: только в разделе «ЗОНА СТУДЕНТА» ниже.
 //  Остальное можно не менять, но на защите нужно уметь объяснить.
 //
 //  Сборка:
 //    Windows, MinGW / Code::Blocks:  g++ server.cpp -o server.exe -lws2_32
 //    Windows, Visual Studio:         просто собрать проект (библиотека
 //                                    подключается строкой #pragma ниже)
 //    Linux / macOS:                  g++ server.cpp -o server
 // =====================================================================

// ---------------------------------------------------------------------
//  Подключение сетевых библиотек. В Windows сокеты называются Winsock,
//  в Linux/macOS — POSIX sockets. Вызовы (socket, bind, listen, accept,
//  recv, send) одинаковые, отличаются только заголовочные файлы и пара
//  мелочей. Этот блок прячет различия — его менять не нужно.
// ---------------------------------------------------------------------
#ifdef _WIN32
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601        // Windows 7+: нужно старому MinGW для inet_pton/inet_ntop
    #endif
    #include <winsock2.h>              // основные функции Winsock
    #include <ws2tcpip.h>              // inet_ntop, socklen_t
    #include <windows.h>               // SetConsoleOutputCP
    #pragma comment(lib, "ws2_32.lib") // для Visual Studio: подключить библиотеку
    #define SEND_FLAGS 0
#else
    #include <sys/socket.h>            // socket, bind, listen, accept, send, recv
    #include <netinet/in.h>            // struct sockaddr_in, htons, INADDR_ANY
    #include <arpa/inet.h>             // inet_ntop
    #include <unistd.h>                // close
    typedef int SOCKET;                // в Linux сокет — это просто int (дескриптор)
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket close
    #define SEND_FLAGS MSG_NOSIGNAL    // не «падать», если клиент внезапно отключился
#endif

#include <iostream>
#include <string>
#include <sstream>   // istringstream — удобно разбирать команду на слова
#include <map>       // map и vector пригодятся в вариантах (словари, списки)
#include <vector>
#include <cstdlib>   // rand, srand
#include <ctime>     // time

using namespace std;

const int PORT = 9000;   // номер порта сервера (как в практических работах)

// =====================================================================
//  ЗОНА СТУДЕНТА — здесь пишется логика вашего варианта
// =====================================================================

int rockCount = 0;
int paperCount = 0;
int scissorsCount = 0;

string moveName(int move)
{
    if (move == 1) return "Rock";
    if (move == 2) return "Paper";
    return "Scissors";
}


string onConnect()
{
    rockCount = 0;
    paperCount = 0;
    scissorsCount = 0;

    return "Welcome to Rock, Paper, Scissors! "
           "Enter 1 - Rock, 2 - Paper, 3 - Scissors. "
           "Enter QUIT to exit.";
}

string handle(const string& command)
{
    if (command != "1" && command != "2" && command != "3") {
        return "ERROR: Enter 1, 2, or 3.";
    }

    int playerMove = command[0] - '0';
    int serverMove;

    if (playerMove == 1 && rockCount >= 3) {
        serverMove = 2;
        rockCount = 0;
    }
    else if (playerMove == 2 && paperCount >= 3) {
        serverMove = 3;
        paperCount = 0;
    }
    else if (playerMove == 3 && scissorsCount >= 3) {
        serverMove = 1;
        scissorsCount = 0;
    }
    else {
        serverMove = rand() % 3 + 1;
    }

    // Увеличиваем счётчик выбранного игроком хода
    if (playerMove == 1) {
        rockCount++;
    }
    else if (playerMove == 2) {
        paperCount++;
    }
    else {
        scissorsCount++;
    }

    // Определяем результат игры
    string result;

    if (playerMove == serverMove) {
        result = "It's a draw!";
    }
    else if ((playerMove == 1 && serverMove == 3) ||
             (playerMove == 2 && serverMove == 1) ||
             (playerMove == 3 && serverMove == 2)) {
        result = "You win!";
    }
    else {
        result = "You lose!";
    }

    return "You chose: " + moveName(playerMove) +
           ". Server chose: " + moveName(serverMove) +
           ". " + result;
}

// =====================================================================
//  КОНЕЦ ЗОНЫ СТУДЕНТА
// =====================================================================


// ---------------------------------------------------------------------
//  Отправить строку и добавить в конце '\n'.
//  Почему цикл: send() может отправить НЕ ВСЕ байты за один вызов
//  (частичная отправка) — он возвращает, сколько реально ушло.
//  Поэтому досылаем остаток, пока не отправим всё.
//  Возвращает false, если соединение разорвано.
// ---------------------------------------------------------------------
bool sendLine(SOCKET s, const string& text)
{
    string data = text + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(s, data.c_str() + sent, (int)(data.size() - sent), SEND_FLAGS);
        if (n <= 0) return false;   // ошибка или соединение закрыто
        sent += n;
    }
    return true;
}

// ---------------------------------------------------------------------
//  Принять одну строку (до символа '\n').
//  Почему по одному байту: TCP передаёт ПОТОК байтов, а не сообщения.
//  Две команды могут прийти «склеенными», а одна — по частям.
//  Граница сообщения у нас — символ '\n', поэтому читаем, пока его
//  не встретим. Это просто и надёжно (хотя и не самый быстрый способ).
//  Символ '\r' пропускаем: его добавляют некоторые программы (telnet, Windows).
//  Возвращает false, если клиент закрыл соединение (recv вернул 0)
//  или произошла ошибка (recv вернул -1).
// ---------------------------------------------------------------------
bool recvLine(SOCKET s, string& line)
{
    line.clear();
    char c;
    while (true) {
        int n = recv(s, &c, 1, 0);  // ждём (блокируемся), пока не придёт 1 байт
        if (n <= 0) return false;
        if (c == '\n') return true;
        if (c != '\r') line += c;
    }
}

int main()
{
#ifdef _WIN32
    // В Windows сокеты нужно «включить» перед использованием: WSAStartup
    // загружает библиотеку Winsock версии 2.2. В Linux этого шага нет.
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cout << "WSAStartup failed" << endl;
        return 1;
    }
    SetConsoleOutputCP(CP_UTF8);     // чтобы русские буквы выводились нормально
#endif
    srand((unsigned)time(nullptr));  // для вариантов, где нужны случайные числа

    // 1. socket() — создаём сокет.
    //    AF_INET     — адреса IPv4;
    //    SOCK_STREAM — потоковый сокет, то есть TCP.
    //    Система возвращает дескриптор — «номерок», по которому
    //    мы дальше обращаемся к этому сокету.
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == INVALID_SOCKET) {
        cout << "socket() error" << endl;
        return 1;
    }

#ifndef _WIN32
    // SO_REUSEADDR: разрешить сразу перезапустить сервер на том же порту.
    // Без этого после остановки сервера порт какое-то время занят
    // соединениями в состоянии TIME_WAIT, и bind() вернёт ошибку.
    // (В Windows эта опция работает иначе, поэтому там её не ставим.)
    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#endif

    // 2. bind() — «привязываем» сокет к адресу и порту.
    //    sin_family — семейство адресов (IPv4);
    //    sin_port   — порт; htons() переводит число в СЕТЕВОЙ порядок
    //                 байтов (big-endian), так требует протокол;
    //    sin_addr   — INADDR_ANY = 0.0.0.0, то есть принимать подключения
    //                 на всех сетевых интерфейсах компьютера (и 127.0.0.1,
    //                 и адрес в локальной сети).
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cout << "bind() error: port " << PORT << " is busy?" << endl;
        closesocket(listenSock);
        return 1;
    }

    // 3. listen() — переводим сокет в режим ожидания подключений
    //    (состояние LISTEN, его видно в netstat -an).
    //    5 — размер очереди: сколько клиентов могут ждать, пока мы
    //    не вызвали accept().
    if (listen(listenSock, 5) == SOCKET_ERROR) {
        cout << "listen() error" << endl;
        closesocket(listenSock);
        return 1;
    }

    cout << "Server is listening on port " << PORT << endl;

    // Главный цикл: обслуживаем клиентов ПО ОЧЕРЕДИ, одного за другим.
    // Такой сервер называется итеративным. Пока обслуживается один
    // клиент, второй ждёт в очереди listen().
    while (true) {
        // 4. accept() — ждём (блокируемся), пока не подключится клиент.
        //    К этому моменту система уже выполнила тройное рукопожатие TCP.
        //    accept() возвращает НОВЫЙ сокет — только для общения с этим
        //    клиентом. Слушающий сокет listenSock продолжает слушать порт.
        sockaddr_in clientAddr = {};
        socklen_t clientLen = sizeof(clientAddr);
        SOCKET client = accept(listenSock, (sockaddr*)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) {
            cout << "accept() error" << endl;
            continue;
        }

        // Узнаём адрес и порт клиента. inet_ntop переводит адрес из
        // двоичного вида в текст "127.0.0.1", ntohs — порт из сетевого
        // порядка байтов обратно в обычное число.
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        int clientPort = ntohs(clientAddr.sin_port);
        cout << "Client connected: " << ip << ":" << clientPort << endl;

        // 5. Обмен данными: приветствие, затем «команда — ответ».
        sendLine(client, onConnect());

        string command;
        while (recvLine(client, command)) {
            cout << "  <- " << command << endl;   // журнал на экране сервера

            if (command == "QUIT") {
                sendLine(client, "BYE");
                break;
            }

            string answer = handle(command);
            cout << "  -> " << answer << endl;
            if (!sendLine(client, answer)) break;
        }

        // 6. closesocket() — закрываем соединение с этим клиентом.
        //    Сторона, которая закрывает первой, проходит через TIME_WAIT.
        closesocket(client);
        cout << "Client disconnected: " << ip << ":" << clientPort << endl;
    }

    // Сюда программа не доходит (сервер работает до Ctrl+C),
    // но так выглядит правильное завершение:
    closesocket(listenSock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
