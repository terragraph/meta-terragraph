"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[7715],{3905:(e,r,t)=>{t.d(r,{Zo:()=>m,kt:()=>k});var a=t(7294);function o(e,r,t){return r in e?Object.defineProperty(e,r,{value:t,enumerable:!0,configurable:!0,writable:!0}):e[r]=t,e}function n(e,r){var t=Object.keys(e);if(Object.getOwnPropertySymbols){var a=Object.getOwnPropertySymbols(e);r&&(a=a.filter((function(r){return Object.getOwnPropertyDescriptor(e,r).enumerable}))),t.push.apply(t,a)}return t}function l(e){for(var r=1;r<arguments.length;r++){var t=null!=arguments[r]?arguments[r]:{};r%2?n(Object(t),!0).forEach((function(r){o(e,r,t[r])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(t)):n(Object(t)).forEach((function(r){Object.defineProperty(e,r,Object.getOwnPropertyDescriptor(t,r))}))}return e}function i(e,r){if(null==e)return{};var t,a,o=function(e,r){if(null==e)return{};var t,a,o={},n=Object.keys(e);for(a=0;a<n.length;a++)t=n[a],r.indexOf(t)>=0||(o[t]=e[t]);return o}(e,r);if(Object.getOwnPropertySymbols){var n=Object.getOwnPropertySymbols(e);for(a=0;a<n.length;a++)t=n[a],r.indexOf(t)>=0||Object.prototype.propertyIsEnumerable.call(e,t)&&(o[t]=e[t])}return o}var p=a.createContext({}),c=function(e){var r=a.useContext(p),t=r;return e&&(t="function"==typeof e?e(r):l(l({},r),e)),t},m=function(e){var r=c(e.components);return a.createElement(p.Provider,{value:r},e.children)},d={inlineCode:"code",wrapper:function(e){var r=e.children;return a.createElement(a.Fragment,{},r)}},s=a.forwardRef((function(e,r){var t=e.components,o=e.mdxType,n=e.originalType,p=e.parentName,m=i(e,["components","mdxType","originalType","parentName"]),s=c(t),k=o,v=s["".concat(p,".").concat(k)]||s[k]||d[k]||n;return t?a.createElement(v,l(l({ref:r},m),{},{components:t})):a.createElement(v,l({ref:r},m))}));function k(e,r){var t=arguments,o=r&&r.mdxType;if("string"==typeof e||o){var n=t.length,l=new Array(n);l[0]=s;var i={};for(var p in r)hasOwnProperty.call(r,p)&&(i[p]=r[p]);i.originalType=e,i.mdxType="string"==typeof e?e:o,l[1]=i;for(var c=2;c<n;c++)l[c]=t[c];return a.createElement.apply(null,l)}return a.createElement.apply(null,t)}s.displayName="MDXCreateElement"},4051:(e,r,t)=>{t.r(r),t.d(r,{assets:()=>p,contentTitle:()=>l,default:()=>d,frontMatter:()=>n,metadata:()=>i,toc:()=>c});var a=t(7462),o=(t(7294),t(3905));const n={},l="Terragraph Developer Manual",i={unversionedId:"developer/README",id:"developer/README",title:"Terragraph Developer Manual",description:"1. Overview",source:"@site/../docs/developer/README.md",sourceDirName:"developer",slug:"/developer/",permalink:"/docs/developer/",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/developer/README.md",tags:[],version:"current",frontMatter:{},sidebar:"developerManualSidebar",next:{title:"Overview",permalink:"/docs/developer/Overview"}},p={},c=[],m={toc:c};function d(e){let{components:r,...t}=e;return(0,o.kt)("wrapper",(0,a.Z)({},m,t,{components:r,mdxType:"MDXLayout"}),(0,o.kt)("h1",{id:"terragraph-developer-manual"},"Terragraph Developer Manual"),(0,o.kt)("h1",{id:"table-of-contents"},"Table of Contents"),(0,o.kt)("ol",null,(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Overview"},"Overview")),(0,o.kt)("li",{parentName:"ol"},"Architecture",(0,o.kt)("ol",{parentName:"li"},(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Communication_Protocol"},"Communication Protocol")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Routing_Layer"},"Routing Layer")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Driver_Interface"},"Driver Interface")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Driver_Stack"},"Driver Stack")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/VPP_Implementation"},"VPP Implementation")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Timing_Synchronization"},"Timing and Synchronization")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/PTP_SyncE"},"PTP & SyncE")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/WiFi"},"Wi-Fi")))),(0,o.kt)("li",{parentName:"ol"},"Firmware Layer",(0,o.kt)("ol",{parentName:"li"},(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Beamforming_Link_Adaptation"},"Beamforming and Link Adaptation")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/MAC_PHY_Specification"},"MAC & PHY Specification")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/PHY_Algorithms"},"PHY Algorithms")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Firmware_Stats"},"Firmware Stats")))),(0,o.kt)("li",{parentName:"ol"},"End-to-End (E2E) Service",(0,o.kt)("ol",{parentName:"li"},(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Topology_Management"},"Topology Management")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Network_Ignition"},"Network Ignition")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Software_Upgrade"},"Software Upgrade")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Configuration_Management"},"Configuration Management")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Scans"},"Scans")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Network_Measurements"},"Network Measurements")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Prefix_Allocation"},"Prefix Allocation")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Topology_Discovery"},"Topology Discovery")))),(0,o.kt)("li",{parentName:"ol"},"Application Layer Modules",(0,o.kt)("ol",{parentName:"li"},(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Stats_Events_Logs"},"Stats, Events, Logs")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Terragraph_CLI"},"Terragraph CLI")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/API_Service"},"API Service")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Local_Web_Interface"},"Local Web Interface")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/LED_Agent"},"LED Agent")))),(0,o.kt)("li",{parentName:"ol"},"System Management",(0,o.kt)("ol",{parentName:"li"},(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Service_Scripts"},"Service Scripts")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Watchdog"},"Watchdog")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/High_Availability"},"High Availability")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Security"},"Security")))),(0,o.kt)("li",{parentName:"ol"},"Version Control",(0,o.kt)("ol",{parentName:"li"},(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Release_Conventions"},"Release Conventions")),(0,o.kt)("li",{parentName:"ol"},(0,o.kt)("a",{parentName:"li",href:"/docs/developer/Firmware_Versioning"},"Firmware Versioning"))))))}d.isMDXComponent=!0}}]);