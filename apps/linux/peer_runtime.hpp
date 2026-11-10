#pragma once
#include <extrapane/service.hpp>
#include <QObject>
#include <memory>
#include <thread>
class QQmlEngine;
class LinuxPeerRuntime final : public QObject {
    Q_OBJECT
    struct State;
    std::shared_ptr<State> state_;
    struct Ui;
    std::unique_ptr<Ui> ui_;
    std::jthread inventory_;
    void openDisplay(std::shared_ptr<void> use, std::uint16_t port);

public:
    LinuxPeerRuntime(QQmlEngine*, QString receiverAvailability, QObject* parent = nullptr);
    ~LinuxPeerRuntime() override;
    extrapane::peers::Service::Hooks hooks();
    void refreshInventory();
    std::vector<extrapane::peers::Offer> offers() const;
    void shutdown();
signals:
    void inventoryChanged();
    void inventoryFailed(QString reason);
};
