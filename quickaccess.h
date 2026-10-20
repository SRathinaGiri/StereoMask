#ifndef QUICKACCESS_H
#define QUICKACCESS_H

#include <QPointer>
#include <QStringList>
#include <QWidget>

class QAction;
class QSettings;
class QToolButton;
class QVBoxLayout;

class QuickAccess : public QWidget
{
    Q_OBJECT
public:
    explicit QuickAccess(QSettings *settings, QWidget *parent = nullptr);
    void registerCommand(const QString &id, const QString &category, QAction *action);
    void attachButton(QToolButton *button, const QString &id);
    void restore();
    QStringList commandIds() const { return m_ids; }
    void addCommand(const QString &id);
    void removeCommand(const QString &id);
    void moveCommand(const QString &id, int offset);

public slots:
    void manage();

private:
    struct Command { QString id; QString category; QPointer<QAction> action; };
    const Command *command(const QString &id) const;
    void save();
    void rebuild();
    QSettings *m_settings;
    QList<Command> m_commands;
    QStringList m_ids;
    QVBoxLayout *m_buttons;
};

#endif
