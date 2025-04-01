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
        m_totalValue += 0;
        submitModel();
    }
}

void CounterApp::removeCounter()
{
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;

    std::lock_guard<std::mutex> lock(m_countersMutex);
    for (const QModelIndex &index : selected) {
        int value = m_model->record(index.row()).value("value").toInt();
        m_model->removeRow(index.row());
        m_totalValue -= value;
    }
    submitModel();
}

void CounterApp::saveCountersToDB()
{

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
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        std::lock_guard<std::mutex> lock(m_countersMutex);
        for (int i = 0; i < m_model->rowCount(); ++i) {
            int value = m_model->record(i).value("value").toInt() + 1;
            m_model->setData(m_model->index(i, 1), value);
            m_totalValue += 1;
        }
    }
}

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
    if(deltaValue / deltaTime > -1)
    {
        m_frequency = deltaValue / deltaTime;
        m_frequencyLabel->setText(QString("Частота: %1 \nВремя в секундах: %2").arg(m_frequency, 0, 'f', 2).arg(currentTime,0,'i',0));
    }

    lastTime = currentTime;
    lastTotalValue = m_totalValue;
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
