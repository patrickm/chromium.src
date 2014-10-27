// Copyright 2014 The ChromeOS IME Authors. All Rights Reserved.
// limitations under the License.
// See the License for the specific language governing permissions and
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// distributed under the License is distributed on an "AS-IS" BASIS,
// Unless required by applicable law or agreed to in writing, software
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// You may obtain a copy of the License at
// you may not use this file except in compliance with the License.
// Licensed under the Apache License, Version 2.0 (the "License");
//
goog.provide('i18n.input.chrome.inputview.elements.content.ModifierKey');

goog.require('goog.dom.TagName');
goog.require('goog.dom.classlist');
goog.require('i18n.input.chrome.inputview.Css');
goog.require('i18n.input.chrome.inputview.StateType');
goog.require('i18n.input.chrome.inputview.elements.ElementType');
goog.require('i18n.input.chrome.inputview.elements.content.FunctionalKey');



goog.scope(function() {
var StateType = i18n.input.chrome.inputview.StateType;



/**
 * The modifier key.
 *
 * @param {string} id the id.
 * @param {string} name the name.
 * @param {string} iconCssClass The css class for the icon.
 * @param {!i18n.input.chrome.inputview.StateType} toState The state.
 * @param {!i18n.input.chrome.inputview.StateManager} stateManager The state
 *     manager.
 * @param {boolean} supportSticky True if this modifier key supports sticky.
 * @param {goog.events.EventTarget=} opt_eventTarget The event target.
 * @constructor
 * @extends {i18n.input.chrome.inputview.elements.content.FunctionalKey}
 */
i18n.input.chrome.inputview.elements.content.ModifierKey = function(id, name,
    iconCssClass, toState, stateManager, supportSticky, opt_eventTarget) {
  goog.base(this, id,
      i18n.input.chrome.inputview.elements.ElementType.MODIFIER_KEY,
      name, iconCssClass, opt_eventTarget);

  /**
   * The state when click on the key.
   *
   * @type {!i18n.input.chrome.inputview.StateType}
   */
  this.toState = toState;

  /**
   * The state manager.
   *
   * @type {!i18n.input.chrome.inputview.StateManager}
   * @private
   */
  this.stateManager_ = stateManager;

  /**
   * True if supports sticky.
   *
   * @type {boolean}
   */
  this.supportSticky = supportSticky;

  if (supportSticky) {
    this.pointerConfig.dblClick = true;
    this.pointerConfig.longPressWithPointerUp = true;
    this.pointerConfig.longPressDelay = 1200;
  }
};
goog.inherits(i18n.input.chrome.inputview.elements.content.ModifierKey,
    i18n.input.chrome.inputview.elements.content.FunctionalKey);
var ModifierKey = i18n.input.chrome.inputview.elements.content.ModifierKey;


/**
 * The dot icon for capslock.
 *
 * @type {!Element}
 * @private
 */
ModifierKey.prototype.dotIcon_;


/**
 * True if a double click is just happened.
 *
 * @type {boolean}
 */
ModifierKey.prototype.isDoubleClicking = false;


/** @override */
ModifierKey.prototype.createDom = function() {
  goog.base(this, 'createDom');

  if (this.toState == i18n.input.chrome.inputview.StateType.CAPSLOCK ||
      this.supportSticky) {
    var dom = this.getDomHelper();
    this.dotIcon_ = dom.createDom(goog.dom.TagName.DIV,
        i18n.input.chrome.inputview.Css.CAPSLOCK_DOT);
    dom.appendChild(this.tableCell, this.dotIcon_);
  }

  this.setAriaLabel(this.getChromeVoxMessage());
};


/** @override */
ModifierKey.prototype.update = function() {
  var isStateEnabled = this.stateManager_.hasState(this.toState);
  var isSticky = this.stateManager_.isSticky(this.toState);
  this.setHighlighted(isStateEnabled);
  if (this.dotIcon_) {
    if (isStateEnabled && isSticky) {
      goog.dom.classlist.add(this.dotIcon_,
          i18n.input.chrome.inputview.Css.CAPSLOCK_DOT_HIGHLIGHT);
    } else {
      goog.dom.classlist.remove(this.dotIcon_,
          i18n.input.chrome.inputview.Css.CAPSLOCK_DOT_HIGHLIGHT);
    }
  }
};


/** @override */
ModifierKey.prototype.getChromeVoxMessage = function() {
  switch (this.toState) {
    case StateType.SHIFT:
      return chrome.i18n.getMessage('SHIFT');
    case StateType.CAPSLOCK:
      return chrome.i18n.getMessage('CAPSLOCK');
    case StateType.ALTGR:
      return chrome.i18n.getMessage('ALTGR');
  }
  return '';
};

});  // goog.scope
