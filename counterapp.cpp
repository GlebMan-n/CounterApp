#include "counterapp.h"

CounterApp::CounterApp(QWidget *parent)
    : QMainWindow(parent)
{
    // Инициализация UI
    setupUi();

    // Подключение к БД
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName("counters.db");
    if (!m_db.open()) {
        QMessageBox::critical(this, "Ошибка", m_db.lastError().text());
        return;
    }

    // Создание таблицы в БД, если её нет
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS counters (id INTEGER PRIMARY KEY, value INTEGER)");
    //Таблица для бэкапа данных перед записью
    query.exec("CREATE TABLE IF NOT EXISTS counters_backup (id INTEGER PRIMARY KEY, value INTEGER)");

    // Загрузка счетчиков из БД
    loadCountersFromDB();

    // Запуск потока для инкремента
    m_incrementThread = std::make_unique<std::thread>(&CounterApp::incrementCounters, this);

    // Таймер для обновления частоты
    m_updateFrequencyTimer = new QTimer(this);
    connect(m_updateFrequencyTimer, &QTimer::timeout, this, &CounterApp::updateFrequencyLabel);
    m_updateFrequencyTimer->start(1000);
}

// Деструктор для корректной остановки потока
CounterApp::~CounterApp()
{
    m_incrementThread.reset();
    m_db.close();
}

void CounterApp::setupUi()
{
    m_table = new QTableWidget(0, 1, this);
    m_table->setHorizontalHeaderLabels({"Счетчик"});

    m_addButton = new QPushButton("Добавить");
    m_removeButton = new QPushButton("Удалить");
    m_saveButton = new QPushButton("Сохранить");

    m_frequencyLabel = new QLabel("Частота: 0.0");

    auto* layout = new QVBoxLayout;
    layout->addWidget(m_table);
    layout->addWidget(m_frequencyLabel);
    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addWidget(m_saveButton);
    layout->addLayout(buttonLayout);

    setCentralWidget(new QWidget);
    centralWidget()->setLayout(layout);

    // Сигналы и слоты
    connect(m_addButton, &QPushButton::clicked, this, &CounterApp::addCounter);
    connect(m_removeButton, &QPushButton::clicked, this, &CounterApp::removeCounter);
    connect(m_saveButton, &QPushButton::clicked, this, &CounterApp::saveCountersToDB);

}

// Добавление счетчика
void CounterApp::addCounter() {
    std::lock_guard<std::mutex> lock(m_countersMutex);
    m_counters.push_back(0);
    m_table->setRowCount(m_counters.size());
    m_table->setItem(m_counters.size() - 1, 0, new QTableWidgetItem(QString::number(0)));
    m_totalValue += 0;
}

// Удаление счетчика
void CounterApp::removeCounter()
{
    if (m_table->currentRow() == -1) return;

    std::lock_guard<std::mutex> lock(m_countersMutex);
    int value = m_counters[m_table->currentRow()];
    m_counters.erase(m_counters.begin() + m_table->currentRow());
    m_table->removeRow(m_table->currentRow());
    m_totalValue -= value;
}

// Сохранение в БД
void CounterApp::saveCountersToDB()
{
    //Проверим корректность данных перед сохранением
    if(m_counters.size() == 0)
    {
        QMessageBox::critical(this, "Ошибка, счетчики пусты", m_db.lastError().text());
        return;
    }

    QSqlQuery query;
    //Очищаем прошлые данные
    query.exec("DELETE FROM counters_backup");
    //Делаем бэкап на всякий случай
    query.exec("INSERT INTO counters_backup SELECT * FROM counters;");
    //Очищаем данные
    query.exec("DELETE FROM counters");

    //Сохраняем текущее состояние счетчиков
    std::lock_guard<std::mutex> lock(m_countersMutex);
    for (size_t i = 0; i < m_counters.size(); ++i) {
        query.prepare("INSERT INTO counters (value) VALUES (?)");
        query.addBindValue(m_counters[i]);
        query.exec();
    }
}

// Загрузка из БД
void CounterApp::loadCountersFromDB() {
    QSqlQuery query("SELECT value FROM counters");

    while (query.next()) {
        m_counters.push_back(query.value(0).toInt());
        m_table->setRowCount(m_counters.size());
        m_table->setItem(m_counters.size() - 1, 0,
                       new QTableWidgetItem(QString::number(m_counters.back())));
        m_totalValue += m_counters.back();
    }
}

// Поток инкремента счетчиков
void CounterApp::incrementCounters()
{
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        std::lock_guard<std::mutex> lock(m_countersMutex);
        for (size_t i = 0; i < m_counters.size(); ++i) {
            m_counters[i]++;
            m_table->item(i, 0)->setText(QString::number(m_counters[i]));
            m_totalValue += 1;
        }
    }
}

// Обновление частоты
void CounterApp::updateFrequencyLabel()
{
    static int lastTotalValue = 0;
    static double lastTime = 0.0;

    double currentTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;

    if (lastTime == 0.0) {
        lastTime = currentTime;
        lastTotalValue = m_totalValue;
        return;
    }

    double deltaValue = m_totalValue - lastTotalValue;
    double deltaTime = currentTime - lastTime;

    m_frequency = deltaValue / deltaTime;

    m_frequencyLabel->setText(QString("Частота: %1").arg(m_frequency, 0, 'f', 2));

    lastTime = currentTime;
    lastTotalValue = m_totalValue;
}
