"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[9342],{3905:(e,t,n)=>{n.d(t,{Zo:()=>s,kt:()=>m});var r=n(7294);function a(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function l(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var r=Object.getOwnPropertySymbols(e);t&&(r=r.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,r)}return n}function i(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?l(Object(n),!0).forEach((function(t){a(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):l(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function o(e,t){if(null==e)return{};var n,r,a=function(e,t){if(null==e)return{};var n,r,a={},l=Object.keys(e);for(r=0;r<l.length;r++)n=l[r],t.indexOf(n)>=0||(a[n]=e[n]);return a}(e,t);if(Object.getOwnPropertySymbols){var l=Object.getOwnPropertySymbols(e);for(r=0;r<l.length;r++)n=l[r],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(a[n]=e[n])}return a}var p=r.createContext({}),d=function(e){var t=r.useContext(p),n=t;return e&&(n="function"==typeof e?e(t):i(i({},t),e)),n},s=function(e){var t=d(e.components);return r.createElement(p.Provider,{value:t},e.children)},u={inlineCode:"code",wrapper:function(e){var t=e.children;return r.createElement(r.Fragment,{},t)}},c=r.forwardRef((function(e,t){var n=e.components,a=e.mdxType,l=e.originalType,p=e.parentName,s=o(e,["components","mdxType","originalType","parentName"]),c=d(n),m=a,k=c["".concat(p,".").concat(m)]||c[m]||u[m]||l;return n?r.createElement(k,i(i({ref:t},s),{},{components:n})):r.createElement(k,i({ref:t},s))}));function m(e,t){var n=arguments,a=t&&t.mdxType;if("string"==typeof e||a){var l=n.length,i=new Array(l);i[0]=c;var o={};for(var p in t)hasOwnProperty.call(t,p)&&(o[p]=t[p]);o.originalType=e,o.mdxType="string"==typeof e?e:a,i[1]=o;for(var d=2;d<l;d++)i[d]=n[d];return r.createElement.apply(null,i)}return r.createElement.apply(null,n)}c.displayName="MDXCreateElement"},8955:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>p,contentTitle:()=>i,default:()=>u,frontMatter:()=>l,metadata:()=>o,toc:()=>d});var r=n(7462),a=(n(7294),n(3905));const l={},i="LED Agent",o={unversionedId:"developer/LED_Agent",id:"developer/LED_Agent",title:"LED Agent",description:"This document describes the LED controller daemon that runs on Puma hardware.",source:"@site/../docs/developer/LED_Agent.md",sourceDirName:"developer",slug:"/developer/LED_Agent",permalink:"/docs/developer/LED_Agent",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/developer/LED_Agent.md",tags:[],version:"current",frontMatter:{},sidebar:"developerManualSidebar",previous:{title:"Local Web Interface",permalink:"/docs/developer/Local_Web_Interface"},next:{title:"Service Scripts",permalink:"/docs/developer/Service_Scripts"}},p={},d=[{value:"Overview",id:"overview",level:2}],s={toc:d};function u(e){let{components:t,...n}=e;return(0,a.kt)("wrapper",(0,r.Z)({},s,n,{components:t,mdxType:"MDXLayout"}),(0,a.kt)("h1",{id:"led-agent"},"LED Agent"),(0,a.kt)("p",null,"This document describes the LED controller daemon that runs on Puma hardware."),(0,a.kt)("h2",{id:"overview"},"Overview"),(0,a.kt)("p",null,"When ",(0,a.kt)("inlineCode",{parentName:"p"},"envParams.LED_AGENT_ENABLED")," is set in the node configuration, the\n",(0,a.kt)("inlineCode",{parentName:"p"},"led-agent")," daemon will run and control the three green LED lights beneath Puma\nunits using GPIO pins (",(0,a.kt)("inlineCode",{parentName:"p"},"/sys/class/gpio"),")."),(0,a.kt)("p",null,"The behavior of the LEDs is as follows:"),(0,a.kt)("table",null,(0,a.kt)("thead",{parentName:"table"},(0,a.kt)("tr",{parentName:"thead"},(0,a.kt)("th",{parentName:"tr",align:null},"LED"),(0,a.kt)("th",{parentName:"tr",align:null},"GPIO"),(0,a.kt)("th",{parentName:"tr",align:null},"Possible States"))),(0,a.kt)("tbody",{parentName:"table"},(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"A"),(0,a.kt)("td",{parentName:"tr",align:null},"505"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("ul",null,(0,a.kt)("li",null,(0,a.kt)("inlineCode",{parentName:"td"},"ON"),": The node is powered on (specifically, ",(0,a.kt)("inlineCode",{parentName:"td"},"led-agent")," is running).")))),(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"B"),(0,a.kt)("td",{parentName:"tr",align:null},"506"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("ul",null,(0,a.kt)("li",null,(0,a.kt)("inlineCode",{parentName:"td"},"ON"),": At least one link is associated."),(0,a.kt)("li",null,(0,a.kt)("inlineCode",{parentName:"td"},"OFF"),": No links are associated.")))),(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"C"),(0,a.kt)("td",{parentName:"tr",align:null},"502"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("ul",null,(0,a.kt)("li",null,(0,a.kt)("inlineCode",{parentName:"td"},"ON"),": All links are of good RF quality."),(0,a.kt)("li",null,(0,a.kt)("inlineCode",{parentName:"td"},"BLINK"),": At least one link exhibits poor RF quality."),(0,a.kt)("li",null,(0,a.kt)("inlineCode",{parentName:"td"},"OFF"),": No links are associated, or RF quality could not be determined.")))))),(0,a.kt)("p",null,'Link quality is determined by the MCS reported via firmware stats. A "good" link\nmust be operating at MCS 9 or higher.'),(0,a.kt)("p",null,(0,a.kt)("inlineCode",{parentName:"p"},"led-agent")," subscribes to stats published by ",(0,a.kt)("inlineCode",{parentName:"p"},"driver-if")," to obtain link quality\ndata."))}u.isMDXComponent=!0}}]);