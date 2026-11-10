#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <array>
#include <memory>

class QProcess;
class QTimer;

// Windows receiver renderer choice (D3D11/D3D12/Vulkan/OpenGL) for the shared UI.
// Support is checked by running `dm-view renderer-info`; options it does not
// report as available are disabled. D3D11 is the default and the fallback, and
// dm-view also falls back to D3D11 if a chosen renderer fails at session start.
class RendererSettings final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString renderer READ renderer NOTIFY changed)
    Q_PROPERTY(QVariantList options READ options NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    // `program` is dm-view; `settings` stores the "Renderer" value.
    RendererSettings(QString program,std::unique_ptr<QSettings> settings,QObject* parent=nullptr);
    ~RendererSettings() override;
    QString renderer()const{return renderer_;}
    QVariantList options()const;
    bool checking()const{return process_!=nullptr;}
    bool checked()const{return checked_;}
    QString message()const{return message_;}
    // Saves an available renderer; returns false (and keeps the old one) otherwise.
    Q_INVOKABLE bool setRenderer(QString id);
    // Starts a support check unless one is running.
    Q_INVOKABLE void check();
    // Probe parsing, public for tests.
    void applyProbeLine(const QString& line);
    void finishProbe(const QString& failure={});
signals:
    void changed();
private:
    struct Option{QString id,label,detail;bool available=false,reported=false;};
    std::array<Option,4> options_;
    QString program_,renderer_="d3d11",message_;
    std::unique_ptr<QSettings> settings_;
    QProcess* process_=nullptr;
    QTimer* timeout_=nullptr;
    QByteArray pending_;
    bool checked_=false;
    Option* find(const QString& id);
    void save(const QString& id);
    void reset();
};
