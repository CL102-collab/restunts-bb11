(() => {
    const contexts = ['Racing', 'Replay', 'Menus'];
    const actionCodeBase = 36;
    let catalog;

    function bindingCall(name, returnType, argumentTypes = [], argumentsList = []) {
        return Module['ccall'](name, returnType, argumentTypes, argumentsList);
    }

    function bindingLabels(name) {
        const labels = [];
        for (let index = 0; ; index++) {
            const label = bindingCall(name, 'string', ['number'], [index]);
            if (!label) return labels;
            labels.push(label);
        }
    }

    function bindingCatalog() {
        return catalog ||= {
            defaults: bindingCall('restunts_controller_bindings_defaults', 'string'),
            controls: bindingLabels('restunts_controller_binding_control_label'),
            actions: bindingLabels('restunts_controller_binding_action_label'),
            firstControl: bindingCall('restunts_controller_binding_first_control', 'number')
        };
    }

    function validateBindings(encoded) {
        if (typeof encoded !== 'string') return null;
        const { defaults } = bindingCatalog();
        if (encoded.length !== defaults.length || encoded === defaults) return null;
        return bindingCall('restunts_controller_bindings_valid', 'number', ['string'], [encoded]) ? encoded : null;
    }

    function openBindings(encoded, onSave) {
        const { defaults, controls, actions, firstControl } = bindingCatalog();
        const prefixLength = defaults.length - contexts.length * controls.length;
        const edited = [...(validateBindings(encoded) ?? defaults)];
        const dialog = document.createElement('dialog');
        dialog.className = 'controller-bindings-dialog';
        dialog.setAttribute('aria-labelledby', 'controller-bindings-title');
        dialog.innerHTML = `
            <style>
                .controller-bindings-dialog {
                    box-sizing: border-box; width: min(36rem, calc(100% - 2rem)); max-height: calc(100% - 2rem);
                    border: 1px solid #63748a; border-radius: 8px; padding: 16px; background: #1b2635; color: inherit;
                }
                .controller-bindings-dialog::backdrop { background: rgb(0 0 0 / .65); }
                .controller-bindings-dialog form {
                    display: grid; grid-template-rows: auto auto minmax(0, 1fr) auto;
                    gap: 12px; max-height: min(34rem, calc(100dvh - 4rem));
                }
                .controller-bindings-dialog h2 { margin: 0; }
                .controller-bindings-tabs, .controller-bindings-actions { display: flex; flex-wrap: wrap; gap: 8px; }
                .controller-bindings-tabs [aria-selected="true"] { border-color: #e8b55b; }
                .controller-bindings-fields { overflow-y: auto; padding: 4px; }
                .controller-binding {
                    display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.2fr);
                    align-items: center; gap: 8px; margin-bottom: 6px;
                }
                .controller-bindings-fields select { min-width: 0; width: 100%; }
                .controller-bindings-actions .controller-bindings-save { margin-left: auto; background: #145b99; }
            </style>
            <form>
                <h2 id="controller-bindings-title">Controller bindings</h2>
                <div class="controller-bindings-tabs" role="tablist" aria-label="Binding context"></div>
                <div class="controller-bindings-actions">
                    <button type="button" class="controller-bindings-restore">Restore defaults</button>
                    <button type="submit" class="controller-bindings-save">Save</button>
                    <button type="button" class="controller-bindings-cancel">Cancel</button>
                </div>
            </form>`;
        const tabs = dialog.querySelector('[role="tablist"]');
        const footer = dialog.querySelector('.controller-bindings-actions');
        const contextsUi = [];
        const choices = [];
        let selectedContext = 0;

        function selectContext(index) {
            selectedContext = index;
            contextsUi.forEach(({ tab, panel }, context) => {
                const selected = index === context;
                tab.setAttribute('aria-selected', selected);
                tab.tabIndex = selected ? 0 : -1;
                panel.hidden = !selected;
            });
        }

        contexts.forEach((name, context) => {
            const tab = document.createElement('button');
            tab.type = 'button';
            tab.id = 'controller-bindings-tab-' + context;
            tab.textContent = name;
            tab.setAttribute('role', 'tab');
            const panel = document.createElement('div');
            panel.id = 'controller-bindings-panel-' + context;
            panel.className = 'controller-bindings-fields';
            panel.setAttribute('role', 'tabpanel');
            panel.setAttribute('aria-labelledby', tab.id);
            tab.setAttribute('aria-controls', panel.id);
            tab.addEventListener('click', () => selectContext(context));
            contextsUi.push({ tab, panel });
            tabs.append(tab);
            footer.before(panel);

            // Match the desktop dialog by listing the triggers and stick axes first.
            for (let row = 0; row < controls.length; row++) {
                const control = (row + firstControl) % controls.length;
                const slot = prefixLength + context * controls.length + control;
                const field = document.createElement('div');
                field.className = 'controller-binding';
                const label = document.createElement('label');
                const select = document.createElement('select');
                select.id = 'controller-binding-' + context + '-' + control;
                label.htmlFor = select.id;
                label.textContent = controls[control];
                actions.forEach((actionLabel, action) => {
                    if (bindingCall('controller_binding_allowed', 'number', ['number', 'number', 'number'],
                        [context, control, action])) {
                        select.add(new Option(actionLabel, action.toString(actionCodeBase).toUpperCase()));
                    }
                });
                select.value = edited[slot];
                select.addEventListener('change', () => { edited[slot] = select.value; });
                choices.push({ select, slot });
                field.append(label, select);
                panel.append(field);
            }
        });

        selectContext(0);
        tabs.addEventListener('keydown', event => {
            const destinations = {
                ArrowLeft: (selectedContext + contexts.length - 1) % contexts.length,
                ArrowRight: (selectedContext + 1) % contexts.length,
                Home: 0,
                End: contexts.length - 1
            };
            if (!Object.hasOwn(destinations, event.key)) return;
            event.preventDefault();
            selectContext(destinations[event.key]);
            contextsUi[selectedContext].tab.focus();
        });
        dialog.querySelector('.controller-bindings-restore').addEventListener('click', () => {
            choices.forEach(({ select, slot }) => { select.value = edited[slot] = defaults[slot]; });
        });
        dialog.querySelector('.controller-bindings-cancel').addEventListener('click', () => dialog.close());
        dialog.addEventListener('submit', event => {
            event.preventDefault();
            onSave(validateBindings(edited.join('')));
            dialog.close();
        });
        dialog.addEventListener('close', () => dialog.remove(), { once: true });
        document.body.append(dialog);
        dialog.showModal();
    }

    Module['validateRestuntsControllerBindings'] = validateBindings;
    Module['openRestuntsControllerBindings'] = openBindings;
})();
