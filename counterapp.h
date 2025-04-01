#ifndef COUNTERAPP_H
#define COUNTERAPP_H

#include <QMainWindow>
#include <QSqlTableModel>
#include <QTableView>
#include <QDateTime>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlDatabase>
#include <QSqlError>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>
#include <QItemSelectionModel>
#include <thread>
#include <mutex>
#include <atomic>

class CounterApp : public QMainWindow
{
    Q_OBJECT

public:
    CounterApp(QWidget *parent = nullptr);
    ~CounterApp();
    void setupUi();
    void addCounter();
    void removeCounter();
    void saveCountersToDB();
    void loadCountersFromDB();
    void incrementCounters();
    void updateFrequencyLabel();
    void submitModel();
    int getTotalValue();
private:
    QTableView* m_tableView;
    QSqlTableModel* m_model;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_saveButton;
    QLabel* m_frequencyLabel;
    QSqlDatabase m_db;
    std::unique_ptr<std::thread> m_incrementThread;
    QTimer* m_updateFrequencyTimer;

    std::mutex m_countersMutex;
   // std::atomic<double> m_frequency{0.0};
    int m_lastTotalValue{0};
    double m_lastTime{0.0};
    bool m_pause{false};
};

#endif // COUNTERAPP_H
