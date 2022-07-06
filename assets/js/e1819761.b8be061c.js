"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[1237],{3905:function(e,t,n){n.d(t,{Zo:function(){return u},kt:function(){return m}});var r=n(7294);function a(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function i(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var r=Object.getOwnPropertySymbols(e);t&&(r=r.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,r)}return n}function l(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?i(Object(n),!0).forEach((function(t){a(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):i(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function o(e,t){if(null==e)return{};var n,r,a=function(e,t){if(null==e)return{};var n,r,a={},i=Object.keys(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||(a[n]=e[n]);return a}(e,t);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(a[n]=e[n])}return a}var p=r.createContext({}),s=function(e){var t=r.useContext(p),n=t;return e&&(n="function"==typeof e?e(t):l(l({},t),e)),n},u=function(e){var t=s(e.components);return r.createElement(p.Provider,{value:t},e.children)},d={inlineCode:"code",wrapper:function(e){var t=e.children;return r.createElement(r.Fragment,{},t)}},c=r.forwardRef((function(e,t){var n=e.components,a=e.mdxType,i=e.originalType,p=e.parentName,u=o(e,["components","mdxType","originalType","parentName"]),c=s(n),m=a,f=c["".concat(p,".").concat(m)]||c[m]||d[m]||i;return n?r.createElement(f,l(l({ref:t},u),{},{components:n})):r.createElement(f,l({ref:t},u))}));function m(e,t){var n=arguments,a=t&&t.mdxType;if("string"==typeof e||a){var i=n.length,l=new Array(i);l[0]=c;var o={};for(var p in t)hasOwnProperty.call(t,p)&&(o[p]=t[p]);o.originalType=e,o.mdxType="string"==typeof e?e:a,l[1]=o;for(var s=2;s<i;s++)l[s]=n[s];return r.createElement.apply(null,l)}return r.createElement.apply(null,n)}c.displayName="MDXCreateElement"},6301:function(e,t,n){n.r(t),n.d(t,{assets:function(){return u},contentTitle:function(){return p},default:function(){return m},frontMatter:function(){return o},metadata:function(){return s},toc:function(){return d}});var r=n(3117),a=n(102),i=(n(7294),n(3905)),l=["components"],o={},p="Wi-Fi",s={unversionedId:"developer/WiFi",id:"developer/WiFi",title:"Wi-Fi",description:"This document describes Terragraph's Wi-Fi architecture on Puma.",source:"@site/../docs/developer/WiFi.md",sourceDirName:"developer",slug:"/developer/WiFi",permalink:"/docs/developer/WiFi",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/developer/WiFi.md",tags:[],version:"current",frontMatter:{},sidebar:"developerManualSidebar",previous:{title:"PTP & SyncE",permalink:"/docs/developer/PTP_SyncE"},next:{title:"Beamforming and Link Adaptation",permalink:"/docs/developer/Beamforming_Link_Adaptation"}},u={},d=[{value:"Overview",id:"overview",level:2},{value:"ESP32 Firmware",id:"esp32-firmware",level:2},{value:"SLIP",id:"slip",level:2},{value:"Resources",id:"resources",level:2}],c={toc:d};function m(e){var t=e.components,n=(0,a.Z)(e,l);return(0,i.kt)("wrapper",(0,r.Z)({},c,n,{components:t,mdxType:"MDXLayout"}),(0,i.kt)("h1",{id:"wi-fi"},"Wi-Fi"),(0,i.kt)("p",null,"This document describes Terragraph's Wi-Fi architecture on Puma."),(0,i.kt)("h2",{id:"overview"},"Overview"),(0,i.kt)("p",null,"Puma uses an ESP32 chip (ESP32-WROOM-32U) from Espressif Systems for Wi-Fi. Two\nserial ports are used:"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"/dev/ttyS0")," - Linux console access via Telnet"),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"/dev/ttyS2")," - SLIP port")),(0,i.kt)("p",null,"The following IP addresses are configured by default:"),(0,i.kt)("table",null,(0,i.kt)("thead",{parentName:"table"},(0,i.kt)("tr",{parentName:"thead"},(0,i.kt)("th",{parentName:"tr",align:null},"IP Address"),(0,i.kt)("th",{parentName:"tr",align:null},"Description"))),(0,i.kt)("tbody",{parentName:"table"},(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"192.168.4.1"),(0,i.kt)("td",{parentName:"tr",align:null},"Wi-Fi AP")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"192.168.4.x"),(0,i.kt)("td",{parentName:"tr",align:null},"Client/station IP assigned by ESP32 DHCP")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"192.168.5.2"),(0,i.kt)("td",{parentName:"tr",align:null},"SLIP interface endpoint in ESP32")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"192.168.5.1"),(0,i.kt)("td",{parentName:"tr",align:null},"SLIP interface endpoint in NXP")))),(0,i.kt)("p",null,"Wi-Fi is only used for administrative purposes and does not carry any user\ntraffic."),(0,i.kt)("h2",{id:"esp32-firmware"},"ESP32 Firmware"),(0,i.kt)("p",null,"The ESP32 firmware is built via ",(0,i.kt)("inlineCode",{parentName:"p"},"recipes-wifi/esp-fw/esp-fw_0.1.bb"),". This uses\n",(0,i.kt)("a",{parentName:"p",href:"https://github.com/espressif/esp-idf"},"ESP-IDF"),", the official development framework. Sources are located in\n",(0,i.kt)("inlineCode",{parentName:"p"},"recipes-wifi/esp-fw/files/src/"),"."),(0,i.kt)("p",null,"Updating ESP32 firmware is done through the following scripts:"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"make_esp32_cfg.sh")," - Generates the binary config-partition image\n(",(0,i.kt)("inlineCode",{parentName:"li"},"esp32config.bin"),")"),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"flash_esp32.sh")," - Flashes the application, configuration, etc.")),(0,i.kt)("p",null,"The default firmware binaries are installed in ",(0,i.kt)("inlineCode",{parentName:"p"},"/usr/share/esp32/"),":"),(0,i.kt)("table",null,(0,i.kt)("thead",{parentName:"table"},(0,i.kt)("tr",{parentName:"thead"},(0,i.kt)("th",{parentName:"tr",align:null},"Binary"),(0,i.kt)("th",{parentName:"tr",align:null},"Purpose"))),(0,i.kt)("tbody",{parentName:"table"},(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"bootloader.bin")),(0,i.kt)("td",{parentName:"tr",align:null},"bootloader")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"partitions_singleapp.bin")),(0,i.kt)("td",{parentName:"tr",align:null},"partition table")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"wifi_softAP.bin")),(0,i.kt)("td",{parentName:"tr",align:null},"application")))),(0,i.kt)("h2",{id:"slip"},"SLIP"),(0,i.kt)("p",null,"SLIP (Serial Line Internet Protocol) is used to enable TCP/IP on the serial port\n",(0,i.kt)("inlineCode",{parentName:"p"},"/dev/ttyS2"),". This is configured via node configuration keys ",(0,i.kt)("inlineCode",{parentName:"p"},"envParams.SLIP_*"),",\nalong with two ",(0,i.kt)("inlineCode",{parentName:"p"},"sv")," services installed through\n",(0,i.kt)("inlineCode",{parentName:"p"},"recipes-wifi/esp-slip/esp-slip_0.1.bb"),":"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"slip")," - wrapper for ",(0,i.kt)("inlineCode",{parentName:"li"},"slattach")),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"slip_route")," - IP configuration (run once)")),(0,i.kt)("p",null,"SLIP speeds are limited based on the configured baud rate. This is 115200 by\ndefault, but can be increased to 576000 (through a patch to ",(0,i.kt)("inlineCode",{parentName:"p"},"slattach")," in\n",(0,i.kt)("inlineCode",{parentName:"p"},"recipes-extended/net-tools/files/0000-Add-higher-baud-rates.patch"),")."),(0,i.kt)("h2",{id:"resources"},"Resources"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("a",{parentName:"li",href:"https://github.com/espressif/esp-idf"},"ESP-IDF")," - Espressif IoT Development Framework")))}m.isMDXComponent=!0}}]);