// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class OllamaClient;
class QQmlEngine;
class QJSEngine;

// The models Ask uses, for Settings and the Ask page: who answers (Ollama on this computer, or Claude with an API key
// kept in the keyring), where Ollama is, which models answer and embed, whether Ollama is reachable and has them, and
// whether it keeps both models loaded. One instance, created in main().
//
// `answerModel` is the model that answers now (the Claude model when Claude answers); `ollamaAnswerModel` is the Ollama
// one chosen in Settings.
class ModelSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Models)
    QML_SINGLETON
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY settingsChanged)
    Q_PROPERTY(bool claude READ claude NOTIFY settingsChanged)
    Q_PROPERTY(QString answerModel READ answerModel NOTIFY settingsChanged)
    Q_PROPERTY(QString ollamaAnswerModel READ ollamaAnswerModel WRITE setOllamaAnswerModel NOTIFY settingsChanged)
    Q_PROPERTY(QString claudeModel READ claudeModel WRITE setClaudeModel NOTIFY settingsChanged)
    Q_PROPERTY(QStringList claudeModels READ claudeModels CONSTANT)
    Q_PROPERTY(bool claudeKeySet READ claudeKeySet NOTIFY claudeKeyChanged)
    Q_PROPERTY(QString claudeKeyError READ claudeKeyError NOTIFY claudeKeyChanged)
    Q_PROPERTY(bool keyringAvailable READ keyringAvailable CONSTANT)
    Q_PROPERTY(QString embeddingModel READ embeddingModel WRITE setEmbeddingModel NOTIFY settingsChanged)
    Q_PROPERTY(QString defaultAnswerModel READ defaultAnswerModel CONSTANT)
    Q_PROPERTY(QString defaultEmbeddingModel READ defaultEmbeddingModel CONSTANT)
    Q_PROPERTY(bool checking READ checking NOTIFY statusChanged)
    Q_PROPERTY(bool checked READ checked NOTIFY statusChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QStringList installed READ installed NOTIFY statusChanged)
    Q_PROPERTY(QStringList loaded READ loaded NOTIFY statusChanged)
    Q_PROPERTY(bool answerModelInstalled READ answerModelInstalled NOTIFY statusChanged)
    Q_PROPERTY(bool embeddingModelInstalled READ embeddingModelInstalled NOTIFY statusChanged)
    Q_PROPERTY(bool swapping READ swapping NOTIFY statusChanged)

public:
    static ModelSettings *createInstance(QObject *parent);
    static ModelSettings *instance();
    static ModelSettings *create(QQmlEngine *, QJSEngine *);

    OllamaClient *client() const
    {
        return m_client;
    }

    QString url() const;
    void setUrl(const QString &url);
    QString provider() const;
    void setProvider(const QString &provider);
    bool claude() const;
    QString answerModel() const;
    QString ollamaAnswerModel() const;
    void setOllamaAnswerModel(const QString &model);
    QString claudeModel() const;
    void setClaudeModel(const QString &model);
    QStringList claudeModels() const;
    bool claudeKeySet() const;
    QString claudeKeyError() const
    {
        return m_claudeKeyError;
    }
    bool keyringAvailable() const;
    // Keeps the key in the keyring (an empty one forgets it)
    Q_INVOKABLE void setClaudeKey(const QString &key);
    QString embeddingModel() const;
    void setEmbeddingModel(const QString &model);
    QString defaultAnswerModel() const;
    QString defaultEmbeddingModel() const;

    bool checking() const
    {
        return m_checking;
    }
    bool checked() const
    {
        return m_checked;
    }
    bool reachable() const
    {
        return m_reachable;
    }
    QString error() const
    {
        return m_error;
    }
    QStringList installed() const
    {
        return m_installed;
    }
    QStringList loaded() const
    {
        return m_loaded;
    }
    bool answerModelInstalled() const;
    bool embeddingModelInstalled() const;
    bool swapping() const
    {
        return m_swapping;
    }

    // Asks Ollama what it has installed and loaded
    Q_INVOKABLE void refresh();
    // After an answer: if the answer model is no longer loaded, Ollama is swapping models for every question
    void checkSwapping();

Q_SIGNALS:
    void settingsChanged();
    void statusChanged();
    void claudeKeyChanged();

private:
    explicit ModelSettings(QObject *parent);
    bool has(const QString &model) const;
    void loadClaudeKey();

    OllamaClient *m_client;
    bool m_checking = false;
    quint64 m_refresh = 0; // a newer refresh (another Ollama address) makes an older one's replies stale
    bool m_checked = false;
    bool m_reachable = false;
    QString m_error;
    QStringList m_installed;
    QStringList m_loaded;
    bool m_swapping = false;
    QString m_claudeKeyError;
};
