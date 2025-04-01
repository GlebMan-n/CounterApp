#include "counterapp.h"

CounterApp::CounterApp(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName("counters.db");
    if (!m_db.open()) {
        QMessageBox::critical(this, "Ошибка", m_db.lastError().text());
        return;
    }

    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS counters (id INTEGER PRIMARY KEY, value INTEGER)");
    query.exec("CREATE TABLE IF NOT EXISTS counters_backup (id INTEGER PRIMARY KEY, value INTEGER)");

    query.exec("DELETE FROM counters");
    query.exec("INSERT INTO counters SELECT * FROM counters_backup;");

    m_model = new QSqlTableModel(this, m_db);
    m_model->setTable("counters");
    m_model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_model->select();
    m_model->setHeaderData(1, Qt::Horizontal, "Счетчик");

    m_tableView->setModel(m_model);
    m_tableView->setColumnHidden(0, true);
}

CounterApp::~CounterApp()
{
    m_incrementThread.reset();
    m_db.close();
}

void CounterApp::setupUi()
{
    m_tableView = new QTableView(this);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_addButton = new QPushButton("Добавить");
    m_removeButton = new QPushButton("Удалить");
    m_saveButton = new QPushButton("Сохранить");

    m_frequencyLabel = new QLabel("Частота: 0.0 \nВремя в секундах: 0");

    auto* layout = new QVBoxLayout;
    layout->addWidget(m_tableView);
    layout->addWidget(m_frequencyLabel);
    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addWidget(m_saveButton);
    layout->addLayout(buttonLayout);

    setCentralWidget(new QWidget);
    centralWidget()->setLayout(layout);

    connect(m_addButton, &QPushButton::clicked, this, &CounterApp::addCounter);
    connect(m_removeButton, &QPushButton::clicked, this, &CounterApp::removeCounter);
    connect(m_saveButton, &QPushButton::clicked, this, &CounterApp::saveCountersToDB);

    // Запуск потока для инкремента
    m_incrementThread = std::make_unique<std::thread>(&CounterApp::incrementCounters, this);

    // Таймер для обновления частоты
    m_updateFrequencyTimer = new QTimer(this);
    connect(m_updateFrequencyTimer, &QTimer::timeout, this, &CounterApp::updateFrequencyLabel);
    m_updateFrequencyTimer->start(1000);
    this->setWindowTitle(tr("Показания счетчиков"));
}

void CounterApp::addCounter() {
    std::lock_guard<std::mutex> lock(m_countersMutex);
    QSqlRecord record = m_model->record();
    record.setValue("value", 0);
    if (m_model->insertRecord(-1, record)) {
        submitModel();
    }
}

void CounterApp::removeCounter()
{
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;

    std::lock_guard<std::mutex> lock(m_countersMutex);
    for (const QModelIndex &index : selected) {
        m_model->removeRow(index.row());
    }
    submitModel();
}

void CounterApp::saveCountersToDB()
{
    m_pause = true;
    if (m_model->rowCount() == 0)
    {
        if (QMessageBox::No == QMessageBox::question(this,
                                                      tr("Счетчики пусты!"),
                                                      tr("Счетчики пусты! Сохранить пустой список счетчиков?")))
        {
            return;
        }
    }

    QSqlQuery query;
    query.exec("DELETE FROM counters_backup");
    query.exec("INSERT INTO counters_backup SELECT * FROM counters;");


    std::lock_guard<std::mutex> lock(m_countersMutex);

    submitModel();
}

void CounterApp::incrementCounters()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    while (true) {
        if(m_pause)
            continue;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard<std::mutex> lock(m_countersMutex);
        for (auto i = 0; i < m_model->rowCount(); ++i) {
            int value = m_model->record(i).value("value").toInt() + 1;
            m_model->setData(m_model->index(i, 1), value);
        }
    }
}

void CounterApp::updateFrequencyLabel()
{
    int totalValue = getTotalValue();
    if(totalValue < 0)
        return;
    double currentTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;

    if (m_lastTime == 0) {
        m_lastTime = currentTime;
        m_lastTotalValue = totalValue;

        return;
    }

    double deltaValue = totalValue - m_lastTotalValue;
    double deltaTime = currentTime - m_lastTime;
    double m_frequency = deltaValue / deltaTime;
    qWarning() << "\n\n";
    qWarning() << "m_frequency: " << m_frequency;
    qWarning() << "totalValue: " << totalValue;
    qWarning() << "deltaValue: " << deltaValue;
    qWarning() << "deltaTime: " << deltaTime;
    qWarning() << "m_lastTime: " << m_lastTime;
    qWarning() << "m_lastTotalValue: " << m_lastTotalValue;
    qWarning() << "currentTime: " << currentTime;
    qWarning() << "\n\n";
    if(m_frequency > -1)
        m_frequencyLabel->setText(QString("Частота: %1 \nВремя в секундах: %2").arg(m_frequency,0,'f',2).arg(currentTime,0,'i',0));
    if(m_frequency > 500)
        qWarning() << "aaa";
    m_lastTime = currentTime;
    m_lastTotalValue = totalValue;
    submitModel();
}

void CounterApp::submitModel()
{
    QModelIndex currentIndex = m_tableView->currentIndex();
    QItemSelection currentSelection = m_tableView->selectionModel()->selection();
    m_model->submitAll();
    m_tableView->setCurrentIndex(currentIndex);
    m_tableView->selectionModel()->select(currentSelection, QItemSelectionModel::Select);
}

int CounterApp::getTotalValue()
{
    QSqlQuery query;
    if (!query.exec("SELECT SUM(value) FROM counters")) {
        qDebug() << "Ошибка выполнения SQL запроса:" << query.lastError().text();
        return -1; // или обработка ошибки
    }

    if (query.next()) {
        bool ok;
        int sum = query.value(0).toInt(&ok);

        if (!ok) {
            qDebug() << "Ошибка преобразования данных";
            return -1;
        }

        qDebug() << "Сумма в колонке:" << sum;
        return sum;
    } else {
        qDebug() << "Нет данных в результате запроса";
    }
     return -1;
}
