/// <reference types="vite/client" />
import './styles.css';
import { controllerStore } from './store';
import { ControllerLink } from './link';
import { createFlightDeckUI } from './ui/flight-deck';

const appElement = document.querySelector<HTMLDivElement>('#app');

if (!appElement) {
  throw new Error('Element #app not found');
}

const link = new ControllerLink({ autoConnect: true });

const ui = createFlightDeckUI({
  onThrottleChange: (val: number): void => {
    controllerStore.setThrottleInput(val);
  },
  onThrottleReset: (): void => {
    link.flushZeroThrottle();
  },
  onArmAction: (arm: boolean): void => {
    link.sendArm(arm);
  },
});

appElement.replaceChildren(ui.element);

controllerStore.subscribe((state) => {
  ui.update(state);
});

ui.update(controllerStore.getState());
