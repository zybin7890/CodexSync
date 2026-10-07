#pragma once
#include <QTranslator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

// A Qt translator backed by bundled UTF-8 text, without an extra build-tool dependency.
class EnglishTranslator final : public QTranslator {
    QJsonObject messages;
public:
    EnglishTranslator() {
        QFile file(QStringLiteral(":/ui/i18n/en.json"));
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("English UI resource is missing");
        const auto document=QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()||document.object().isEmpty()) throw std::runtime_error("English UI resource is invalid");
        messages=document.object();
    }
    QString translate(const char*,const char* source,const char* = nullptr,int = -1) const override {
        return messages.value(QString::fromUtf8(source)).toString();
    }
    bool isEmpty() const override { return messages.isEmpty(); }
};
