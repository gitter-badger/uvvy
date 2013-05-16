#include "timer.h"
#include "timer_engine.h"

using namespace boost::posix_time;

namespace ssu {
namespace async {

const timer::duration_type timer::retry_min = boost::posix_time::milliseconds(500);
const timer::duration_type timer::retry_max = boost::posix_time::minutes(1);
const timer::duration_type timer::fail_max  = boost::posix_time::seconds(20);

//=========================================================
// timer
//=========================================================

static timer::duration_type backoff(timer::duration_type interval, timer::duration_type max_interval = timer::fail_max)
{
	return std::min(interval * 3 / 2, max_interval);
}

timer::timer(ssu::timer_host_state* host)
{
	engine_ = host->create_timer_engine_for(this);
}

void timer::start(duration_type interval)
{
	interval_ = interval;
	active_ = true;
	engine_->start(interval_);
}

void timer::stop()
{
	engine_->stop();
	active_ = false;
}

void timer::restart()
{
	interval_ = backoff(interval_);
	start(interval_);
}

bool timer::has_failed() const
{
}

//=========================================================
// default_timer_engine
//=========================================================

default_timer_engine::default_timer_engine()
{
}

default_timer_engine::~default_timer_engine()
{
	stop();
}

}
}