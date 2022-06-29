/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Button, List } from "react95";

const ButtonMenu = props => {
  const [menuOpen, setMenuOpen] = React.useState(false);

  // Close the menu on any click
  const handleClick = e => {
    if (menuOpen) {
      setMenuOpen(false);
    }
  };
  React.useEffect(() => {
    document.addEventListener("click", handleClick, false);
    return () => {
      document.removeEventListener("click", handleClick, false);
    };
  });

  return (
    <div className="button-menu-container">
      <Button
        className={props.buttonClass}
        onClick={() => setMenuOpen(!menuOpen)}
        active={menuOpen}
        {...props.buttonProps}
      >
        {props.buttonText}
      </Button>
      {menuOpen && (
        <List
          className={`button-menu ${
            props.flipped ? "button-menu-up" : "button-menu-down"
          }`}
          onClick={() => setMenuOpen(false)}
        >
          {props.children}
        </List>
      )}
    </div>
  );
};

export default ButtonMenu;
