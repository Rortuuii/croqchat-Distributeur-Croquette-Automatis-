// Execute tout le script de l'application avec DOM et GATT simules.
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Element {
 constructor(){this.listeners={};this.children=[];this.value='100';this.dataset={};this.classList={toggle(){},add(){}};}
 addEventListener(name,cb){this.listeners[name]=cb;}
 removeEventListener(name){delete this.listeners[name];}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(){this.children=[];}
 setAttribute(){}
 querySelectorAll(){return this.children.flatMap(c=>[c,...c.querySelectorAll()]);}
}
const elements=new Map(), get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
const storage=new Map(),commands=[];
let input='',failSave=false,clock=false,reply;
const tx={addEventListener(name,cb){reply=cb;},removeEventListener(){},async startNotifications(){}};
function notify(line){const b=new TextEncoder().encode(line+'\n');reply({target:{value:new DataView(b.buffer)}});}
const rx={async writeValueWithResponse(packet){input+=new TextDecoder().decode(packet);let i;
 while((i=input.indexOf('\n'))>=0){const line=input.slice(0,i);input=input.slice(i+1);commands.push(line);const cmd=line.split(';')[0];
 queueMicrotask(()=>{
 if(failSave&&cmd==='SET_MEAL'){notify('ERR;code=STORAGE');return;}
 if(cmd==='SET_TIME')clock=true;
 notify('ACK;cmd='+cmd);
 if(cmd==='GET_STATUS')notify('STATUS;w=100;busy=0;fault=0;sensor=1;cal=1;tare=1;clock='+(clock?1:0)+';rfid=none;last=20;scale=1;schedule=1;op=0');
 if(cmd==='GET_MEALS'){notify('MEALS_BEGIN');notify('MEAL;id=1;time=08:00;mass=30;enable=1');notify('MEALS_END');}
 if(cmd==='TARE'||cmd==='CALIBRATE')notify('DONE;cmd='+cmd);
 });
 }
}};
const gatt={connected:false,async connect(){this.connected=true;return {async getPrimaryService(){return {async getCharacteristic(uuid){return uuid.endsWith('0002')?rx:tx;}};}};},disconnect(){this.connected=false;}};
const device={gatt,addEventListener(){},removeEventListener(){}};
const sandbox={TextEncoder,TextDecoder,Uint8Array,DataView,Promise,console,setTimeout,clearTimeout,setInterval,clearInterval,
 document:{getElementById:get,createElement:()=>new Element(),querySelectorAll:()=>[]},
 window:{isSecureContext:true,confirm:()=>true,scrollTo(){},localStorage:{getItem:k=>storage.get(k)||null,setItem:(k,v)=>storage.set(k,v)}},
 navigator:{bluetooth:{requestDevice:async()=>device}}};
vm.createContext(sandbox);
const file=fs.readdirSync('.').find(x=>x.endsWith('.html'));
const script=fs.readFileSync(file,'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
vm.runInContext(script+';globalThis.app={state,ble,stopPolling};',sandbox);
(async()=>{
 assert.ok(get('dispense-btn').disabled);assert.ok(get('tare-btn').disabled);
 await get('connect-btn').listeners.click();
 const {state,stopPolling}=sandbox.app;
 assert.ok(state.bleConnected&&state.haveStatus);assert.equal(state.meals.length,1);assert.ok(!get('dispense-btn').disabled);
 assert.equal(commands[0],'GET_STATUS');assert.ok(commands[1].startsWith('SET_TIME;'));assert.equal(commands[3],'GET_MEALS');
 await get('tare-btn').listeners.click();assert.equal(state.busy,false);
 await get('calibrate-btn').listeners.click();assert.ok(commands.includes('CALIBRATE;mass_ref=100'));
 failSave=true;await get('save-meals-btn').listeners.click();assert.equal(state.saving,false);
 assert.ok(get('toast').textContent.includes('STORAGE'));
 failSave=false;await get('save-meals-btn').listeners.click();assert.equal(state.meals.length,1);
 // Le dernier repas peut etre supprime et seulement apres ACK.
 const remove=get('meal-list').querySelectorAll().find(x=>x.className==='remove-btn');assert.ok(remove);
 await remove.listeners.click();assert.equal(state.meals.length,0);assert.ok(commands.includes('DELETE_MEAL;id=1'));
 await get('connect-btn').listeners.click();
 clock=false;
 const start=commands.length;
 await get('connect-btn').listeners.click();
 assert.ok(state.bleConnected&&state.clock);
 assert.deepEqual(commands.slice(start).map(line=>line.split(';')[0]),['GET_STATUS','SET_TIME','GET_STATUS','GET_MEALS']);
 sandbox.app.ble.onGATTDisconnected();assert.ok(get('dispense-btn').disabled);assert.equal(state.haveStatus,false);
 stopPolling();console.log('Full app: initialization/connect/status/calibration/storage error/save/delete last/disconnect PASS');
})().catch(error=>{sandbox.app.stopPolling();console.error(error);process.exitCode=1;});
