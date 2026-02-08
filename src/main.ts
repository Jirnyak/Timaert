import {mount} from 'svelte';
import App from './App.svelte';
import './index.css';

const app = mount(App, {
	target: document.querySelector('#app')!,
});

export default app;
