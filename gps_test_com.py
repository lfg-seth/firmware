import asyncio
import sys

import winsdk.windows.devices.geolocation as wdg

async def get_location():
    # Ask Windows for permission to access location
    access = await wdg.Geolocator.request_access_async()
    print(f"Access status: {access}")

    # access can be Allowed / Denied / Unspecified (varies by Windows build)
    # If denied, user must enable Location permissions in Settings.
    locator = wdg.Geolocator()
    locator.desired_accuracy_in_meters = 10  # best-effort hint

    pos = await locator.get_geoposition_async()
    c = pos.coordinate

    # Note: accuracy is meters (estimated)
    print(f"Latitude : {c.point.position.latitude:.6f}")
    print(f"Longitude: {c.point.position.longitude:.6f}")
    print(f"Accuracy : {c.accuracy} meters")

    # Some devices provide additional info
    try:
        print(f"Altitude : {c.point.position.altitude:.2f} m")
    except Exception:
        pass

def main():
    try:
        asyncio.run(get_location())
    except PermissionError:
        print("PermissionError: Enable Location access in Windows Settings.")
        sys.exit(2)
    except Exception as e:
        print(f"ERROR: {type(e).__name__}: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
