/**
 * audio.js - BGM via HTML5 Audio, sound effects via js/sfx.js.
 *
 * BGM is unchanged from the live client. What is new is that this file is now
 * the ONE place the game's audio is turned on and off: the Mute button in the
 * lobby and game bars already calls toggleMute(), so routing the effects
 * through here is what makes that button silence them too, without adding a
 * second control the player has to find.
 *
 * BROWSER AUTOPLAY POLICY
 *   Neither an <audio> element nor an AudioContext may make a sound until the
 *   user has interacted with the page. The failure modes differ and both are
 *   silent:
 *     - audio.play() returns a rejected promise (hence the .catch()es below);
 *     - an AudioContext is created in the "suspended" state and simply plays
 *       nothing, without erroring, until resume() is called from a gesture.
 *   installUnlockHooks() covers both, on the first gesture of any kind, and
 *   then takes itself off again.
 */

import { sfx } from './sfx.js';

class AudioManager {
    constructor() {
        this.bgm = null;
        this.bgmPlaying = false;
        this.muted = false;
    }

    startBGM(src) {
        if (this.bgm) {
            this.bgm.pause();
        }
        this.bgm = new Audio(src);
        this.bgm.loop = true;
        this.bgm.volume = 0.4;
        if (!this.muted) {
            this.bgm.play().catch(() => {
                // Autoplay blocked - will retry on user interaction
            });
        }
        this.bgmPlaying = true;
    }

    stopBGM() {
        if (this.bgm) {
            this.bgm.pause();
            this.bgm.currentTime = 0;
        }
        this.bgmPlaying = false;
    }

    toggleMute() {
        this.muted = !this.muted;
        if (this.bgm) {
            this.bgm.muted = this.muted;
        }
        sfx.setMuted(this.muted);
        return this.muted;
    }

    /** Call from a user click handler to satisfy browser autoplay policy */
    resumeFromUserGesture() {
        if (this.bgm && this.bgmPlaying && !this.muted) {
            this.bgm.play().catch(() => {});
        }
        sfx.unlock();
    }
}

export const audio = new AudioManager();

/**
 * Unlock audio on the first user gesture of any kind, once.
 *
 * The title screen's Start button already calls resumeFromUserGesture(), but
 * that is one particular button on one particular screen: a player who lands
 * on the lobby from a reconnect never presses it, and would then play a whole
 * silent match. These listeners are the safety net, and they remove themselves
 * so they cost nothing after the first tap.
 */
export function installUnlockHooks() {
    if (typeof document === 'undefined') return;
    const unlock = () => {
        audio.resumeFromUserGesture();
        document.removeEventListener('pointerdown', unlock);
        document.removeEventListener('touchstart', unlock);
        document.removeEventListener('keydown', unlock);
    };
    document.addEventListener('pointerdown', unlock);
    document.addEventListener('touchstart', unlock, { passive: true });
    document.addEventListener('keydown', unlock);
}
