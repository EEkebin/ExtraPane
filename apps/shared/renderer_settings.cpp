#include "renderer_settings.hpp"
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

namespace {
bool known(const QString& id) {
    return id == "d3d11" || id == "d3d12" || id == "vulkan" || id == "opengl";
}
QString name(const QString& id) {
    return id == "d3d11" ? "D3D11" : id == "d3d12" ? "D3D12" : id == "vulkan" ? "Vulkan" : "OpenGL";
}
// "RENDERER_INFO vulkan available NVIDIA ...; checks" -> adapter "NVIDIA ...".
QString adapter(QString detail) {
    const auto cut = detail.indexOf(';');
    if (cut >= 0) detail.truncate(cut);
    return detail.trimmed();
}
}

RendererSettings::RendererSettings(QString program, std::unique_ptr<QSettings> settings, QObject* parent)
    : QObject(parent), program_(std::move(program)), settings_(std::move(settings)) {
    options_ = {Option{"d3d11", "D3D11 (default)"}, Option{"d3d12", "D3D12"}, Option{"vulkan", "Vulkan"},
                Option{"opengl", "OpenGL"}};
    reset();
    const auto saved = settings_ ? settings_->value("Renderer", "d3d11").toString() : QString("d3d11");
    renderer_ = known(saved) ? saved : "d3d11";
}
RendererSettings::~RendererSettings() {
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
    }
}

void RendererSettings::reset() {
    for (auto& option : options_) {
        option.reported = false;
        option.available = option.id == "d3d11";
        option.detail = option.id == "d3d11" ? QString("Default") : QString("Not checked yet");
    }
}
RendererSettings::Option* RendererSettings::find(const QString& id) {
    for (auto& option : options_)
        if (option.id == id) return &option;
    return nullptr;
}

QVariantList RendererSettings::options() const {
    QVariantList result;
    for (const auto& option : options_)
        result.push_back(QVariantMap{
            {"id", option.id}, {"label", option.label}, {"available", option.available}, {"detail", option.detail}});
    return result;
}

void RendererSettings::save(const QString& id) {
    renderer_ = id;
    if (settings_) {
        settings_->setValue("Renderer", id);
        settings_->sync();
    }
}

bool RendererSettings::setRenderer(QString id) {
    auto* option = find(id);
    if (!option || !option->available) {
        message_ = (option ? name(id) : QString("That renderer")) + " isn't available on this PC.";
        emit changed();
        return false;
    }
    save(id);
    message_.clear();
    emit changed();
    return true;
}

void RendererSettings::check() {
    if (process_) return;
    reset();
    pending_.clear();
    message_.clear();
    process_ = new QProcess(this);
    timeout_ = new QTimer(process_);
    timeout_->setSingleShot(true);
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        pending_ += process_->readAllStandardOutput();
        if (pending_.size() > 65536) {
            process_->kill();
            return;
        }
        for (qsizetype end; (end = pending_.indexOf('\n')) >= 0;) {
            applyProbeLine(QString::fromUtf8(pending_.left(end)).trimmed());
            pending_.remove(0, end + 1);
        }
    });
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!pending_.isEmpty()) applyProbeLine(QString::fromUtf8(pending_).trimmed());
        finishProbe(status == QProcess::NormalExit && code == 0 ? QString{}
                                                                : QString("The renderer check did not finish."));
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishProbe("The renderer check could not start.");
    });
    connect(timeout_, &QTimer::timeout, this, [this] {
        if (process_) process_->kill();
    });
    timeout_->start(20000);
    emit changed();
    process_->start(program_, {"renderer-info"});
}

void RendererSettings::applyProbeLine(const QString& line) {
    static const QString prefix = "RENDERER_INFO ";
    if (!line.startsWith(prefix)) return;
    const auto rest = line.mid(prefix.size());
    const auto space = rest.indexOf(' ');
    if (space < 0) return;
    auto* option = find(rest.left(space));
    if (!option) return;
    const auto state = rest.mid(space + 1);
    if (state.startsWith("available")) {
        option->reported = true;
        option->available = true;
        const auto where = adapter(state.mid(9));
        option->detail = where.isEmpty() ? QString("Available") : "Available on " + where;
    } else if (state.startsWith("unavailable")) {
        option->reported = true;
        option->available = option->id == "d3d11"; /* default stays selectable */
        option->detail = state.mid(11).trimmed();
        if (option->detail.isEmpty()) option->detail = "Unavailable";
    }
    emit changed();
}

void RendererSettings::finishProbe(const QString& failure) {
    if (process_) {
        process_->disconnect(this);
        process_->deleteLater();
        process_ = nullptr;
        timeout_ = nullptr;
    }
    checked_ = true;
    for (auto& option : options_)
        if (!option.reported && option.id != "d3d11") {
            option.available = false;
            option.detail = failure.isEmpty() ? QString("Not reported by the renderer check") : failure;
        }
    message_ = failure;
    // D3D11 stays selectable: it is dm-view's own fallback and the default.
    if (auto* current = find(renderer_); current && !current->available && renderer_ != "d3d11") {
        message_ = name(renderer_) + " isn't available on this PC (" + current->detail + "). Using D3D11.";
        save("d3d11");
    }
    emit changed();
}
