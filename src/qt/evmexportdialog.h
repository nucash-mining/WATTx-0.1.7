// Copyright (c) 2024 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_EVMEXPORTDIALOG_H
#define BITCOIN_QT_EVMEXPORTDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>

class WalletModel;

/**
 * Dialog for exporting EVM-compatible address and private key
 * for use with Ethereum wallets like Rabby/MetaMask
 */
class EvmExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EvmExportDialog(QWidget *parent = nullptr);
    ~EvmExportDialog();

    void setModel(WalletModel *model);

private Q_SLOTS:
    void onAddressSelected(int index);
    void onCopyEvmAddress();
    void onCopyPrivateKey();
    void onShowPrivateKey();

private:
    void setupUI();
    void populateAddresses();
    QString deriveEvmAddress(const QString &pubkey);
    QString getPrivateKeyForAddress(const QString &address);

    WalletModel *model{nullptr};

    QComboBox *addressComboBox;
    QLineEdit *wattxAddressEdit;
    QLineEdit *evmAddressEdit;
    QLineEdit *privateKeyEdit;
    QPushButton *copyEvmButton;
    QPushButton *showKeyButton;
    QPushButton *copyKeyButton;
    QPushButton *closeButton;
    QLabel *warningLabel;

    bool privateKeyVisible{false};
    QString currentPrivateKey;
};

#endif // BITCOIN_QT_EVMEXPORTDIALOG_H
