#ifndef SUBTITLEBURNING_H
#define SUBTITLEBURNING_H

#include <QWidget>

namespace Ui {
class SubtitleBurning;
}

class SubtitleBurning : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleBurning(QWidget *parent = nullptr);
    ~SubtitleBurning();

private:
    Ui::SubtitleBurning *ui;
};

#endif // SUBTITLEBURNING_H
