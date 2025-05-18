module.exports = [
    {
      "type": "heading",
      "defaultValue": "Demonic Descent Configuration"
    },
    {
      "type": "section",
      "items": [
        {
          "type": "heading",
          "defaultValue": "Time and Date"
        },
        {
          "type": "toggle",
          "messageKey": "AmericanDate",
          "label": "Use American date format",
          "defaultValue": true,
          "description": "Set false for '01 Jan', true for 'Jan 01'"
        }
      ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Weather"
            },
            {
                "type": "input",
                "messageKey": "OpenWeatherAPIKey",
                "defaultValue": "",
                "label": "Open Weather API Key",
                "description": "Leave blank to use my personal key, but it may stop working in the future.",
                "attributes": {
                    "type": "text"
                }
            }
        ]
    },
    {
        "type": "submit",
        "defaultValue": "Submit"
    }
  ];
  