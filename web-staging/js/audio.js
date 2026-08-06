/**
 * audio.js - BGM and SFX via HTML5 Audio API
 */

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
        return this.muted;
    }

    /** Call from a user click handler to satisfy browser autoplay policy */
    resumeFromUserGesture() {
        if (this.bgm && this.bgmPlaying && !this.muted) {
            this.bgm.play().catch(() => {});
        }
    }
}

export const audio = new AudioManager();
