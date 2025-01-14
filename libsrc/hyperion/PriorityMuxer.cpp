// STL includes
#include <algorithm>
#include <limits>

// qt incl
#include <QDateTime>
#include <QTimer>

// Hyperion includes
#include <hyperion/PriorityMuxer.h>

// utils
#include <utils/Logger.h>

const int PriorityMuxer::LOWEST_PRIORITY = std::numeric_limits<uint8_t>::max();

PriorityMuxer::PriorityMuxer(int ledCount)
	: QObject()
	, _log(Logger::getInstance("HYPERION"))
	, _currentPriority(PriorityMuxer::LOWEST_PRIORITY)
	, _manualSelectedPriority(256)
	, _activeInputs()
	, _lowestPriorityInfo()
	, _sourceAutoSelectEnabled(true)
	, _updateTimer(new QTimer())
	, _timer(new QTimer())
	, _blockTimer(new QTimer())
{
	// init lowest priority info
	_lowestPriorityInfo.priority       = PriorityMuxer::LOWEST_PRIORITY;
	_lowestPriorityInfo.timeoutTime_ms = -1;
	_lowestPriorityInfo.ledColors      = std::vector<ColorRgb>(ledCount, {0, 0, 0});
	_lowestPriorityInfo.componentId    = hyperion::COMP_COLOR;
	_lowestPriorityInfo.origin         = "System";
	_lowestPriorityInfo.owner          = "";

	_activeInputs[PriorityMuxer::LOWEST_PRIORITY] = _lowestPriorityInfo;

	// adapt to 1s interval for COLOR and EFFECT timeouts > -1
	connect(_timer, &QTimer::timeout, this, &PriorityMuxer::timeTrigger);
	_timer->setSingleShot(true);
	_blockTimer->setSingleShot(true);
	// forward timeRunner signal to prioritiesChanged signal & threading workaround
	connect(this, &PriorityMuxer::timeRunner, this, &PriorityMuxer::prioritiesChanged);
	connect(this, &PriorityMuxer::signalTimeTrigger, this, &PriorityMuxer::timeTrigger);
	connect(this, &PriorityMuxer::activeStateChanged, this, &PriorityMuxer::prioritiesChanged);

	// start muxer timer
	connect(_updateTimer, &QTimer::timeout, this, &PriorityMuxer::setCurrentTime);
	_updateTimer->setInterval(250);
	_updateTimer->start();
}

PriorityMuxer::~PriorityMuxer()
{
}

void PriorityMuxer::setEnable(const bool& enable)
{
	enable ? _updateTimer->start() : _updateTimer->stop();
}

bool PriorityMuxer::setSourceAutoSelectEnabled(const bool& enable, const bool& update)
{
	if(_sourceAutoSelectEnabled != enable)
	{
		// on disable we need to make sure the last priority call to setPriority is still valid
		if(!enable && !_activeInputs.contains(_manualSelectedPriority))
		{
			Warning(_log, "Can't disable auto selection, as the last manual selected priority (%d) is no longer available", _manualSelectedPriority);
			return false;
		}

		_sourceAutoSelectEnabled = enable;
		Debug(_log, "Source auto select is now %s", enable ? "enabled" : "disabled");

		// update _currentPriority if called from external
		if(update)
			setCurrentTime();

		emit autoSelectChanged(enable);
		return true;
	}
	return false;
}

bool PriorityMuxer::setPriority(const uint8_t priority)
{
	if(_activeInputs.contains(priority))
	{
		_manualSelectedPriority = priority;
		// update auto select state -> update _currentPriority
		setSourceAutoSelectEnabled(false);
		return true;
	}
	return false;
}

void PriorityMuxer::updateLedColorsLength(const int& ledCount)
{
	for (auto infoIt = _activeInputs.begin(); infoIt != _activeInputs.end();)
	{
		if (infoIt->ledColors.size() >= 1)
		{
			infoIt->ledColors.resize(ledCount, infoIt->ledColors.at(0));
		}
		++infoIt;
	}
}

QList<int> PriorityMuxer::getPriorities() const
{
	return _activeInputs.keys();
}

bool PriorityMuxer::hasPriority(const int priority) const
{
	return (priority == PriorityMuxer::LOWEST_PRIORITY) ? true : _activeInputs.contains(priority);
}

const PriorityMuxer::InputInfo PriorityMuxer::getInputInfo(const int priority) const
{
	auto elemIt = _activeInputs.find(priority);
	if (elemIt == _activeInputs.end())
	{
		elemIt = _activeInputs.find(PriorityMuxer::LOWEST_PRIORITY);
		if (elemIt == _activeInputs.end())
		{
			// fallback
			return _lowestPriorityInfo;
		}
	}
	return elemIt.value();
}

void PriorityMuxer::registerInput(const int priority, const hyperion::Components& component, const QString& origin, const QString& owner, unsigned smooth_cfg)
{
	// detect new registers
	bool newInput = false;
	if(!_activeInputs.contains(priority))
		newInput = true;

	InputInfo& input     = _activeInputs[priority];
	input.priority       = priority;
	input.timeoutTime_ms = newInput ? -100 : input.timeoutTime_ms;
	input.componentId    = component;
	input.origin         = origin;
	input.smooth_cfg     = smooth_cfg;
	input.owner          = owner;

	if(newInput)
	{
		Debug(_log,"Register new input '%s/%s' with priority %d as inactive", QSTRING_CSTR(origin), hyperion::componentToIdString(component), priority);
		emit priorityChanged(priority, true);
		emit prioritiesChanged();
		return;
	}
}

bool PriorityMuxer::setInput(const int priority, const std::vector<ColorRgb>& ledColors, int64_t timeout_ms)
{
	if(!_activeInputs.contains(priority))
	{
		Error(_log,"setInput() used without registerInput() for priority '%d', probably the priority reached timeout",priority);
		return false;
	}

	// calc final timeout
	if(timeout_ms > 0)
		timeout_ms = QDateTime::currentMSecsSinceEpoch() + timeout_ms;

	InputInfo& input     = _activeInputs[priority];
	// detect active <-> inactive changes
	bool activeChange = false;
	bool active = true;
	if(input.timeoutTime_ms == -100 && timeout_ms != -100)
	{
		activeChange = true;
	}
	else if(timeout_ms == -100 && input.timeoutTime_ms != -100)
	{
		active = false;
		activeChange = true;
	}
	// update input
	input.timeoutTime_ms = timeout_ms;
	input.ledColors      = ledColors;

	// emit active change
	if(activeChange)
	{
		Debug(_log, "Priority %d is now %s", priority, active ? "active" : "inactive");
		emit activeStateChanged(priority, active);
		setCurrentTime();
	}
	return true;
}

bool PriorityMuxer::setInputImage(const int priority, const Image<ColorRgb>& image, int64_t timeout_ms)
{
	if(!_activeInputs.contains(priority))
	{
		Error(_log,"setInputImage() used without registerInput() for priority '%d', probably the priority reached timeout",priority);
		return false;
	}

	// calc final timeout
	if(timeout_ms > 0)
		timeout_ms = QDateTime::currentMSecsSinceEpoch() + timeout_ms;

	InputInfo& input     = _activeInputs[priority];
	// detect active <-> inactive changes
	bool activeChange = false;
	bool active = true;
	if(input.timeoutTime_ms == -100 && timeout_ms != -100)
	{
		activeChange = true;
	}
	else if(timeout_ms == -100 && input.timeoutTime_ms != -100)
	{
		active = false;
		activeChange = true;
	}
	// update input
	input.timeoutTime_ms = timeout_ms;
	input.image          = image;

	// emit active change
	if(activeChange)
	{
		Debug(_log, "Priority %d is now %s", priority, active ? "active" : "inactive");
		emit activeStateChanged(priority, active);
		setCurrentTime();
	}
	return true;
}

bool PriorityMuxer::setInputInactive(const quint8& priority)
{
	Image<ColorRgb> image;
	return setInputImage(priority, image, -100);
}

bool PriorityMuxer::clearInput(const uint8_t priority)
{
	if (priority < PriorityMuxer::LOWEST_PRIORITY && _activeInputs.remove(priority))
	{
		Debug(_log,"Removed source priority %d",priority);
		// on clear success update _currentPriority
		setCurrentTime();
		emit priorityChanged(priority, false);
		emit prioritiesChanged();
		return true;
	}
	return false;
}

void PriorityMuxer::clearAll(bool forceClearAll)
{
	if (forceClearAll)
	{
		_activeInputs.clear();
		_currentPriority = PriorityMuxer::LOWEST_PRIORITY;
		_activeInputs[_currentPriority] = _lowestPriorityInfo;
	}
	else
	{
		for(auto key : _activeInputs.keys())
		{
			const InputInfo info = getInputInfo(key);
			if ((info.componentId == hyperion::COMP_COLOR || info.componentId == hyperion::COMP_EFFECT) && key < PriorityMuxer::LOWEST_PRIORITY-1)
			{
				clearInput(key);
			}
		}
	}
}

void PriorityMuxer::setCurrentTime(void)
{
	const int64_t now = QDateTime::currentMSecsSinceEpoch();
	int newPriority;
	_activeInputs.contains(0) ? newPriority = 0 : newPriority = PriorityMuxer::LOWEST_PRIORITY;

	for (auto infoIt = _activeInputs.begin(); infoIt != _activeInputs.end();)
	{
		if (infoIt->timeoutTime_ms > 0 && infoIt->timeoutTime_ms <= now)
		{
			quint8 tPrio = infoIt->priority;
			infoIt = _activeInputs.erase(infoIt);
			Debug(_log,"Timeout clear for priority %d",tPrio);
			emit priorityChanged(tPrio, false);
			emit prioritiesChanged();
		}
		else
		{
			// timeoutTime of -100 is awaiting data (inactive); skip
			if(infoIt->timeoutTime_ms > -100)
				newPriority = qMin(newPriority, infoIt->priority);

			// call timeTrigger when effect or color is running with timeout > 0, blacklist prio 255
			if(infoIt->priority < 254 && infoIt->timeoutTime_ms > 0 && (infoIt->componentId == hyperion::COMP_EFFECT || infoIt->componentId == hyperion::COMP_COLOR))
				emit signalTimeTrigger(); // as signal to prevent Threading issues

			++infoIt;
		}
	}
	// eval if manual selected prio is still available
	if(!_sourceAutoSelectEnabled)
	{
		if(_activeInputs.contains(_manualSelectedPriority))
		{
			newPriority = _manualSelectedPriority;
		}
		else
		{
			Debug(_log, "The manual selected priority '%d' is no longer available, switching to auto selection", _manualSelectedPriority);
			// update state, but no _currentPriority re-eval
			setSourceAutoSelectEnabled(true, false);
		}
	}
	// apply & emit on change (after apply!)
	bool changed = false;
	if(_currentPriority != newPriority)
		changed = true;

	_currentPriority = newPriority;

	if(changed)
	{
		Debug(_log, "Set visible priority to %d", newPriority);
		emit visiblePriorityChanged(newPriority);
		emit prioritiesChanged();
	}
}

void PriorityMuxer::timeTrigger()
{
	if(_blockTimer->isActive())
	{
		_timer->start(500);
	}
	else
	{
		emit timeRunner();
		_blockTimer->start(1000);
	}
}
