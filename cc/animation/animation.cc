// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/animation/animation.h"

#include <cmath>

#include "base/debug/trace_event.h"
#include "base/strings/string_util.h"
#include "cc/animation/animation_curve.h"

namespace {

// This should match the RunState enum.
static const char* const s_runStateNames[] = {
  "WaitingForTargetAvailability",
  "WaitingForDeletion",
  "Starting",
  "Running",
  "Paused",
  "Finished",
  "Aborted"
};

COMPILE_ASSERT(static_cast<int>(cc::Animation::RunStateEnumSize) ==
               arraysize(s_runStateNames),
               RunState_names_match_enum);

// This should match the TargetProperty enum.
static const char* const s_targetPropertyNames[] = {
  "Transform",
  "Opacity",
  "Filter",
  "ScrollOffset",
  "BackgroundColor"
};

COMPILE_ASSERT(static_cast<int>(cc::Animation::TargetPropertyEnumSize) ==
               arraysize(s_targetPropertyNames),
               TargetProperty_names_match_enum);

}  // namespace

namespace cc {

scoped_ptr<Animation> Animation::Create(
    scoped_ptr<AnimationCurve> curve,
    int animation_id,
    int group_id,
    TargetProperty target_property) {
  return make_scoped_ptr(new Animation(curve.Pass(),
                                       animation_id,
                                       group_id,
                                       target_property)); }

Animation::Animation(scoped_ptr<AnimationCurve> curve,
                     int animation_id,
                     int group_id,
                     TargetProperty target_property)
    : curve_(curve.Pass()),
      id_(animation_id),
      group_(group_id),
      target_property_(target_property),
      run_state_(WaitingForTargetAvailability),
      iterations_(1),
      iteration_start_(0),
      direction_(Normal),
      playback_rate_(1),
      needs_synchronized_start_time_(false),
      received_finished_event_(false),
      suspended_(false),
      is_controlling_instance_(false),
      is_impl_only_(false),
      affects_active_observers_(true),
      affects_pending_observers_(true) {
}

Animation::~Animation() {
  if (run_state_ == Running || run_state_ == Paused)
    SetRunState(Aborted, base::TimeTicks());
}

void Animation::SetRunState(RunState run_state,
                            base::TimeTicks monotonic_time) {
  if (suspended_)
    return;

  char name_buffer[256];
  base::snprintf(name_buffer,
                 sizeof(name_buffer),
                 "%s-%d%s",
                 s_targetPropertyNames[target_property_],
                 group_,
                 is_controlling_instance_ ? "(impl)" : "");

  bool is_waiting_to_start = run_state_ == WaitingForTargetAvailability ||
                             run_state_ == Starting;

  if (is_waiting_to_start && run_state == Running) {
    TRACE_EVENT_ASYNC_BEGIN1(
        "cc", "Animation", this, "Name", TRACE_STR_COPY(name_buffer));
  }

  bool was_finished = is_finished();

  const char* old_run_state_name = s_runStateNames[run_state_];

  if (run_state == Running && run_state_ == Paused)
    total_paused_time_ += (monotonic_time - pause_time_);
  else if (run_state == Paused)
    pause_time_ = monotonic_time;
  run_state_ = run_state;

  const char* new_run_state_name = s_runStateNames[run_state];

  if (!was_finished && is_finished())
    TRACE_EVENT_ASYNC_END0("cc", "Animation", this);

  char state_buffer[256];
  base::snprintf(state_buffer,
                 sizeof(state_buffer),
                 "%s->%s",
                 old_run_state_name,
                 new_run_state_name);

  TRACE_EVENT_INSTANT2("cc",
                       "LayerAnimationController::SetRunState",
                       TRACE_EVENT_SCOPE_THREAD,
                       "Name",
                       TRACE_STR_COPY(name_buffer),
                       "State",
                       TRACE_STR_COPY(state_buffer));
}

void Animation::Suspend(base::TimeTicks monotonic_time) {
  SetRunState(Paused, monotonic_time);
  suspended_ = true;
}

void Animation::Resume(base::TimeTicks monotonic_time) {
  suspended_ = false;
  SetRunState(Running, monotonic_time);
}

bool Animation::IsFinishedAt(base::TimeTicks monotonic_time) const {
  if (is_finished())
    return true;

  if (needs_synchronized_start_time_)
    return false;

  if (playback_rate_ == 0)
    return false;

  return run_state_ == Running && iterations_ >= 0 &&
         iterations_ * curve_->Duration() / std::abs(playback_rate_) <=
             (monotonic_time + time_offset_ - start_time_ - total_paused_time_)
                 .InSecondsF();
}

double Animation::TrimTimeToCurrentIteration(
    base::TimeTicks monotonic_time) const {
  base::TimeTicks trimmed = monotonic_time + time_offset_;

  // Check for valid parameters
  DCHECK(playback_rate_);
  DCHECK_GE(iteration_start_, 0);

  // If we're paused, time is 'stuck' at the pause time.
  if (run_state_ == Paused)
    trimmed = pause_time_;

  // Returned time should always be relative to the start time and should
  // subtract all time spent paused.
  trimmed -= (start_time_ - base::TimeTicks()) + total_paused_time_;

  // If we're just starting or we're waiting on receiving a start time,
  // time is 'stuck' at the initial state.
  if ((run_state_ == Starting && !has_set_start_time()) ||
      needs_synchronized_start_time())
    trimmed = base::TimeTicks() + time_offset_;

  double active_time = (trimmed - base::TimeTicks()).InSecondsF();

  // Return 0 if we are before the start of the animation
  if (active_time < 0)
    return 0;

  // Always return zero if we have no iterations.
  if (!iterations_)
    return 0;

  // Don't attempt to trim if we have no duration.
  if (curve_->Duration() <= 0)
    return 0;

  double repeated_duration = iterations_ * curve_->Duration();
  double active_duration = repeated_duration / std::abs(playback_rate_);
  double start_offset = iteration_start_ * curve_->Duration();

  // Check if we are past active duration
  if (iterations_ > 0 && active_time >= active_duration)
    active_time = active_duration;

  // Calculate the scaled active time
  double scaled_active_time;
  if (playback_rate_ < 0)
    scaled_active_time =
        (active_time - active_duration) * playback_rate_ + start_offset;
  else
    scaled_active_time = active_time * playback_rate_ + start_offset;

  // Calculate the iteration time
  double iteration_time;
  if (scaled_active_time - start_offset == repeated_duration &&
      fmod(iterations_ + iteration_start_, 1) == 0)
    iteration_time = curve_->Duration();
  else
    iteration_time = fmod(scaled_active_time, curve_->Duration());

  // Calculate the current iteration
  int iteration;
  if (scaled_active_time <= 0)
    iteration = 0;
  else if (iteration_time == curve_->Duration())
    iteration = ceil(iteration_start_ + iterations_ - 1);
  else
    iteration = static_cast<int>(scaled_active_time / curve_->Duration());

  // Check if we are running the animation in reverse direction for the current
  // iteration
  bool reverse = (direction_ == Reverse) ||
                 (direction_ == Alternate && iteration % 2 == 1) ||
                 (direction_ == AlternateReverse && iteration % 2 == 0);

  // If we are running the animation in reverse direction, reverse the result
  if (reverse)
    iteration_time = curve_->Duration() - iteration_time;

  return iteration_time;
}

scoped_ptr<Animation> Animation::CloneAndInitialize(
    RunState initial_run_state) const {
  scoped_ptr<Animation> to_return(
      new Animation(curve_->Clone(), id_, group_, target_property_));
  to_return->run_state_ = initial_run_state;
  to_return->iterations_ = iterations_;
  to_return->iteration_start_ = iteration_start_;
  to_return->start_time_ = start_time_;
  to_return->pause_time_ = pause_time_;
  to_return->total_paused_time_ = total_paused_time_;
  to_return->time_offset_ = time_offset_;
  to_return->direction_ = direction_;
  to_return->playback_rate_ = playback_rate_;
  DCHECK(!to_return->is_controlling_instance_);
  to_return->is_controlling_instance_ = true;
  return to_return.Pass();
}

void Animation::PushPropertiesTo(Animation* other) const {
  // Currently, we only push changes due to pausing and resuming animations on
  // the main thread.
  if (run_state_ == Animation::Paused ||
      other->run_state_ == Animation::Paused) {
    other->run_state_ = run_state_;
    other->pause_time_ = pause_time_;
    other->total_paused_time_ = total_paused_time_;
  }
}

}  // namespace cc
