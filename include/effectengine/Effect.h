#pragma once

// Python includes
// collide of qt slots macro
#undef slots
#include "Python.h"
#define slots

// Qt includes
#include <QThread>
#include <QJsonObject>
#include <QSize>
#include <QImage>
#include <QPainter>
#include <QMap>

// Hyperion includes
#include <utils/Components.h>
#include <utils/Image.h>

class Hyperion;
class Logger;

class Effect : public QThread
{
	Q_OBJECT

public:
	friend class EffectModule;

	Effect(Hyperion *hyperion
				, int priority
				, int timeout
				, const QString &script
				, const QString &name
				, const QJsonObject &args = QJsonObject()
				, const QString &imageData = ""
	);
	virtual ~Effect();

	virtual void run();

	int getPriority() const { return _priority; };

	///
	/// @brief Set manual interuption to true,
	///        Note: DO NOT USE QThread::interuption!
	///
	void setInteruptionFlag() { _interupt = true; };

	///
	/// @brief Check if the interuption flag has been set
	/// @return    The flag state
	///
	bool hasInteruptionFlag() { return _interupt; };

	QString getScript() const { return _script; }
	QString getName() const { return _name; }

	int getTimeout() const {return _timeout; }

	QJsonObject getArgs() const { return _args; }

signals:
	void setInput(const int priority, const std::vector<ColorRgb> &ledColors, const int timeout_ms, const bool &clearEffect);
	void setInputImage(const int priority, const Image<ColorRgb> &image, const int timeout_ms, const bool &clearEffect);

private:

	void addImage();

	Hyperion *_hyperion;

	const int _priority;

	const int _timeout;

	const QString _script;
	const QString _name;

	const QJsonObject _args;
	const QString _imageData;

	int64_t _endTime;

	/// Buffer for colorData
	QVector<ColorRgb> _colors;

	Logger *_log;
	// Reflects whenever this effects should interupt (timeout or external request)
	bool _interupt = false;

	QSize           _imageSize;
	QImage          _image;
	QPainter       *_painter;
	QVector<QImage> _imageStack;
};
