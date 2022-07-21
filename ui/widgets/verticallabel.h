// http://stackoverflow.com/questions/9183050/vertical-qlabel-or-the-equivalent

#ifndef VERTICALLABEL_H
#define VERTICALLABEL_H

#include <QLabel>

class VerticalLabel : public QLabel
{
    Q_OBJECT

public:
    explicit VerticalLabel(QWidget *parent=nullptr);
    explicit VerticalLabel(const QString &text, QWidget *parent=nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    QSize sizeHint() const override ;
    QSize minimumSizeHint() const override;
};

#endif // VERTICALLABEL_H
