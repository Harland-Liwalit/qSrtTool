#ifndef OUTPUTMANAGEMENT_H
#define OUTPUTMANAGEMENT_H

#include <QWidget>

namespace Ui {
class OutputManagement;
}

class OutputManagement : public QWidget
{
    Q_OBJECT

public:
    explicit OutputManagement(QWidget *parent = nullptr);
    ~OutputManagement();

private:
    Ui::OutputManagement *ui;
};

#endif // OUTPUTMANAGEMENT_H
