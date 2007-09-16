/*
 * Copyright 2006 Philip Taylor
 * <philip at zaynar.demon.co.uk> / <excors at gmail.com>
 * Distributed under the terms of the GPL (http://www.gnu.org/licenses/gpl.txt)
 */

var profiler_enabled;

var profile_timers = {};
var profile_counters = {};
var profile_samples = 32;
var profile_sampled = 0;
function profile_begin(name)
{
	if (profiler_enabled)
	{
		var t = new Date();
		var ms = t.getMilliseconds() + 1000*(t.getSeconds() + 60 * t.getMinutes());
		profile_timers[name] = (profile_timers[name] === undefined ? 0 : profile_timers[name]) - ms;
	}
}
function profile_count(name)
{
	if (profiler_enabled)
	{
		profile_counters[name] = (profile_counters[name] === undefined ? 1 : profile_counters[name]+1);
	}
}
function profile_end(name)
{
	if (profiler_enabled)
	{
		var t = new Date();
		var ms = t.getMilliseconds() + 1000*(t.getSeconds() + 60 * t.getMinutes()); // fails once an hour
		profile_timers[name] += ms;
	}
}
function profile_report()
{
	if (!profiler_enabled)
	{
		return;
	}
	
	if (++profile_sampled < profile_samples)
	{
		return;
	}
		
	profile_sampled = 0;
	
	var s = "";
	for (var n in profile_timers)
	{
		s += n + ": " + profile_timers[n]/profile_samples + " ms/frame<br/>";
	}
	for (var n in profile_counters)
	{
		s += n + ": " + profile_counters[n]/profile_samples + " /frame<br/>";
	}
	$('profile').innerHTML = s;
	profile_timers = {};
	profile_counters = {};
}

var framerate_buffer = [ ];
var framerate_previous_date;
const framerate_buffer_max = 32;
var framerate_last_update;
const framerate_update_time = 1000;
function framerate_update()
{
	var now = new Date();

	if (! framerate_previous_date)
	{
		framerate_previous_date = now;
		framerate_last_update = now;
		return;
	}
	
	var length = now - framerate_previous_date;
	framerate_previous_date = now;
	framerate_buffer.push(length);
	while (framerate_buffer.length > framerate_buffer_max)
	{
		framerate_buffer.shift();
	}
	
	if (now - framerate_last_update > framerate_update_time)
	{
		framerate_last_update = now;
		var t = 0;
		for (var i = 0; i < framerate_buffer.length; ++i) { t += framerate_buffer[i]; }
		var fps = Math.round(10 * 1000*framerate_buffer.length / t) / 10;

		show_message("<br>" + fps + " fps");

	}
}
