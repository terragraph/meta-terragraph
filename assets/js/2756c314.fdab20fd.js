"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[1458],{3905:(e,t,n)=>{n.d(t,{Zo:()=>d,kt:()=>u});var r=n(7294);function a(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function i(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var r=Object.getOwnPropertySymbols(e);t&&(r=r.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,r)}return n}function o(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?i(Object(n),!0).forEach((function(t){a(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):i(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function l(e,t){if(null==e)return{};var n,r,a=function(e,t){if(null==e)return{};var n,r,a={},i=Object.keys(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||(a[n]=e[n]);return a}(e,t);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(a[n]=e[n])}return a}var s=r.createContext({}),p=function(e){var t=r.useContext(s),n=t;return e&&(n="function"==typeof e?e(t):o(o({},t),e)),n},d=function(e){var t=p(e.components);return r.createElement(s.Provider,{value:t},e.children)},m={inlineCode:"code",wrapper:function(e){var t=e.children;return r.createElement(r.Fragment,{},t)}},c=r.forwardRef((function(e,t){var n=e.components,a=e.mdxType,i=e.originalType,s=e.parentName,d=l(e,["components","mdxType","originalType","parentName"]),c=p(n),u=a,h=c["".concat(s,".").concat(u)]||c[u]||m[u]||i;return n?r.createElement(h,o(o({ref:t},d),{},{components:n})):r.createElement(h,o({ref:t},d))}));function u(e,t){var n=arguments,a=t&&t.mdxType;if("string"==typeof e||a){var i=n.length,o=new Array(i);o[0]=c;var l={};for(var s in t)hasOwnProperty.call(t,s)&&(l[s]=t[s]);l.originalType=e,l.mdxType="string"==typeof e?e:a,o[1]=l;for(var p=2;p<i;p++)o[p]=n[p];return r.createElement.apply(null,o)}return r.createElement.apply(null,n)}c.displayName="MDXCreateElement"},2082:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>s,contentTitle:()=>o,default:()=>m,frontMatter:()=>i,metadata:()=>l,toc:()=>p});var r=n(7462),a=(n(7294),n(3905));const i={},o="Network Measurements",l={unversionedId:"developer/Network_Measurements",id:"developer/Network_Measurements",title:"Network Measurements",description:"This document describes the network performance tools integrated into the E2E",source:"@site/../docs/developer/Network_Measurements.md",sourceDirName:"developer",slug:"/developer/Network_Measurements",permalink:"/docs/developer/Network_Measurements",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/developer/Network_Measurements.md",tags:[],version:"current",frontMatter:{},sidebar:"developerManualSidebar",previous:{title:"Scans",permalink:"/docs/developer/Scans"},next:{title:"Prefix Allocation",permalink:"/docs/developer/Prefix_Allocation"}},s={},p=[{value:"Architecture",id:"architecture",level:2},{value:"Commands",id:"commands",level:2},{value:"Ping",id:"ping",level:3},{value:"iPerf",id:"iperf",level:3}],d={toc:p};function m(e){let{components:t,...n}=e;return(0,a.kt)("wrapper",(0,r.Z)({},d,n,{components:t,mdxType:"MDXLayout"}),(0,a.kt)("h1",{id:"network-measurements"},"Network Measurements"),(0,a.kt)("p",null,"This document describes the network performance tools integrated into the E2E\nlayer."),(0,a.kt)("h2",{id:"architecture"},"Architecture"),(0,a.kt)("p",null,"The controller's ",(0,a.kt)("inlineCode",{parentName:"p"},"TrafficApp")," supports running and managing ping and iPerf\nsessions, and dispatches commands to each participating minion's ",(0,a.kt)("inlineCode",{parentName:"p"},"TrafficApp")," to\nstart or stop these processes. The controller is responsible for maintaining the\nstate of each session in progress. When a session terminates, the participating\nminion(s) will send the console output of the process to the controller, which\nthen forwards the output to the original sender; at this point, the controller\ndiscards the session state."),(0,a.kt)("p",null,"If possible, the controller can optionally use a link-local IPv6 address instead\nof a global address for the destination (e.g. to support higher data rates).\nThese link-local addresses are automatically populated by the controller using\nadjacency information from Open/R."),(0,a.kt)("h2",{id:"commands"},"Commands"),(0,a.kt)("p",null,"All supported commands are described in the sections below."),(0,a.kt)("h3",{id:"ping"},"Ping"),(0,a.kt)("p",null,"The controller supports the ",(0,a.kt)("inlineCode",{parentName:"p"},"ping6")," utility and most of its standard options\n(defined in ",(0,a.kt)("inlineCode",{parentName:"p"},"thrift::PingOptions"),")."),(0,a.kt)("table",null,(0,a.kt)("thead",{parentName:"table"},(0,a.kt)("tr",{parentName:"thead"},(0,a.kt)("th",{parentName:"tr",align:null},"User Operation"),(0,a.kt)("th",{parentName:"tr",align:null},"Command"))),(0,a.kt)("tbody",{parentName:"table"},(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"Start Ping"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("inlineCode",{parentName:"td"},"START_PING"))),(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"Stop Ping"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("inlineCode",{parentName:"td"},"STOP_PING"))),(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"Get Ping Status"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("inlineCode",{parentName:"td"},"GET_PING_STATUS"))))),(0,a.kt)("h3",{id:"iperf"},"iPerf"),(0,a.kt)("p",null,"The controller supports the ",(0,a.kt)("inlineCode",{parentName:"p"},"iperf3")," utility and most of its standard options\n(defined in ",(0,a.kt)("inlineCode",{parentName:"p"},"thrift::IperfOptions"),")."),(0,a.kt)("table",null,(0,a.kt)("thead",{parentName:"table"},(0,a.kt)("tr",{parentName:"thead"},(0,a.kt)("th",{parentName:"tr",align:null},"User Operation"),(0,a.kt)("th",{parentName:"tr",align:null},"Command"))),(0,a.kt)("tbody",{parentName:"table"},(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"Start iPerf"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("inlineCode",{parentName:"td"},"START_IPERF"))),(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"Stop iPerf"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("inlineCode",{parentName:"td"},"STOP_IPERF"))),(0,a.kt)("tr",{parentName:"tbody"},(0,a.kt)("td",{parentName:"tr",align:null},"Get iPerf Status"),(0,a.kt)("td",{parentName:"tr",align:null},(0,a.kt)("inlineCode",{parentName:"td"},"GET_IPERF_STATUS"))))),(0,a.kt)("p",null,"iPerf requires running both a server and client (unlike ping, which has a client\nonly). The steps to initiate a session, in response to a ",(0,a.kt)("inlineCode",{parentName:"p"},"START_IPERF"),"\noperation, are shown below."),(0,a.kt)("ol",null,(0,a.kt)("li",{parentName:"ol"},(0,a.kt)("strong",{parentName:"li"},"Controller")," - The controller issues a ",(0,a.kt)("inlineCode",{parentName:"li"},"START_IPERF_SERVER")," command to the\nminion serving as the iPerf destination."),(0,a.kt)("li",{parentName:"ol"},(0,a.kt)("strong",{parentName:"li"},"Minion (server)")," - The minion chooses an unused port in the range\n","[70001, 70050]",", then forks an iPerf server process and waits for it to\ninitialize. The ",(0,a.kt)("inlineCode",{parentName:"li"},"iperf3")," command is called with ",(0,a.kt)("inlineCode",{parentName:"li"},"--forceflush")," to prevent output\nbuffering. When the minion has read the first byte of output, it sends a\n",(0,a.kt)("inlineCode",{parentName:"li"},"START_IPERF_SERVER_RESP")," notification to the controller containing the iPerf\nserver port it used."),(0,a.kt)("li",{parentName:"ol"},(0,a.kt)("strong",{parentName:"li"},"Controller")," - The controller issues a ",(0,a.kt)("inlineCode",{parentName:"li"},"START_IPERF_CLIENT")," command to the\nminion serving as the iPerf source. At this point, the session gets recorded."),(0,a.kt)("li",{parentName:"ol"},(0,a.kt)("strong",{parentName:"li"},"Minion (client)")," - The minion forks an iPerf client process."),(0,a.kt)("li",{parentName:"ol"},(0,a.kt)("strong",{parentName:"li"},"Minions (both)")," - When the iPerf process exits, each minion sends the\noutput to the controller via the ",(0,a.kt)("inlineCode",{parentName:"li"},"IPERF_OUTPUT")," message."),(0,a.kt)("li",{parentName:"ol"},(0,a.kt)("strong",{parentName:"li"},"Controller")," - The controller forwards each ",(0,a.kt)("inlineCode",{parentName:"li"},"IPERF_OUTPUT")," message to the\noriginal sender. The iPerf session is deleted upon receiving either message.")))}m.isMDXComponent=!0}}]);