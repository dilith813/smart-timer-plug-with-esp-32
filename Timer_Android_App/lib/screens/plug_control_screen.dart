import 'dart:async';
import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';

class PlugControlScreen extends StatefulWidget {
  final String plugId;
  final Map plugData;

  const PlugControlScreen({
    super.key,
    required this.plugId,
    required this.plugData,
  });

  @override
  State<PlugControlScreen> createState() => _PlugControlScreenState();
}

class _PlugControlScreenState extends State<PlugControlScreen> {
  // --- UI Control State (for time pickers) ---
  int hours = 0;
  int minutes = 0;
  int seconds = 0;

  late FixedExtentScrollController hourController;
  late FixedExtentScrollController minuteController;
  late FixedExtentScrollController secondController;

  // --- Live State from Firebase (and derived local state) ---
  // _currentDurationSeconds will represent the *effective* total duration for the current run,
  // which might be the original set duration, or the remaining time if paused/disconnected.
  int _currentDurationSeconds = 0;
  bool _isPaused = false;
  bool _isOn = false; // True when a timer is set and not completed/reset
  int? _startTimeEpochSeconds; // When the current running segment started
  int? _lastSeenEpochSeconds; // Last time ESP reported in

  String _plugName = "Loading...";

  // --- Local Countdown Variables ---
  int _calculatedTimeLeft = 0; // The active countdown value
  StreamSubscription? _firebaseSubscription;
  Timer? _localCountdownTimer;

  // --- Getters for computed state ---
  String get _currentPlugStatus {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final lastSeen = _lastSeenEpochSeconds ?? now;
    final diff = now - lastSeen;

    if (diff > 20) return "Disconnected"; // Plug not seen in last 20 seconds

    // Consider _isOn for the primary state
    if (!_isOn) return "Off";

    if (_calculatedTimeLeft <= 0) return "Idle";
    if (_isPaused) return "Paused";
    return "Running";
  }

  @override
  void initState() {
    super.initState();

    hourController = FixedExtentScrollController(initialItem: hours);
    minuteController = FixedExtentScrollController(initialItem: minutes);
    secondController = FixedExtentScrollController(initialItem: seconds);

    // Initialize local state variables from initial plugData
    _plugName = widget.plugData['name'] ?? "Plug Control";
    _currentDurationSeconds = widget.plugData['durationSeconds'] ?? 0;
    _isPaused = widget.plugData['isPaused'] ?? false;
    _isOn = widget.plugData['isOn'] ?? false;
    _startTimeEpochSeconds =
        widget.plugData['startTime'] != null
            ? (widget.plugData['startTime'] as num).toInt()
            : null;
    _lastSeenEpochSeconds =
        widget.plugData['lastSeen'] != null
            ? (widget.plugData['lastSeen'] as num).toInt()
            : null;

    // --- Firebase Live Listener Setup ---
    final uid = FirebaseAuth.instance.currentUser!.uid;
    final ref = FirebaseDatabase.instance.ref(
      "users/$uid/plugs/${widget.plugId}",
    );

    _firebaseSubscription = ref.onValue.listen(
      (event) {
        if (event.snapshot.exists && event.snapshot.value != null) {
          final data = Map<String, dynamic>.from(event.snapshot.value as Map);
          _updateLocalStateFromFirebase(data);
        }
      },
      onError: (error) {
        print("Firebase stream error: $error");
      },
    );

    _calculateAndStartCountdown();
  }

  void _updateLocalStateFromFirebase(Map<String, dynamic> data) {
    setState(() {
      final oldStatus = _currentPlugStatus; // Capture status BEFORE updating

      _currentDurationSeconds =
          data['durationSeconds'] ?? _currentDurationSeconds;
      _isPaused = data['isPaused'] ?? _isPaused;
      _isOn = data['isOn'] ?? _isOn;
      _startTimeEpochSeconds =
          data['startTime'] != null ? (data['startTime'] as num).toInt() : null;
      _lastSeenEpochSeconds =
          data['lastSeen'] != null ? (data['lastSeen'] as num).toInt() : null;
      _plugName = data['name'] ?? _plugName;

      final newStatus = _currentPlugStatus; // Capture status AFTER updating

      // --- Handle Disconnect/Reconnect ---
      if (oldStatus != "Disconnected" && newStatus == "Disconnected") {
        // Plug just went disconnected. Freeze the timer.
        // The current _calculatedTimeLeft is the remaining time.
        // Update _currentDurationSeconds to store this for when it reconnects.
        // Firebase should also be updated if we want this state to persist
        // across app restarts, but for now we prioritize local responsiveness.
        // ESP should handle this by reporting 'timeRemaining' at disconnect.
        // For local pause, we just update _calculatedTimeLeft and _currentDurationSeconds
        // when this happens.
        _currentDurationSeconds = _calculatedTimeLeft; // Save remaining time
        _startTimeEpochSeconds = null; // Clear start time to stop calculation
        _localCountdownTimer?.cancel(); // Stop local timer
        _localCountdownTimer = null;
        print(
          "Plug disconnected. Timer paused at: $_calculatedTimeLeft seconds.",
        );
      } else if (oldStatus == "Disconnected" &&
          newStatus != "Disconnected" &&
          _isOn &&
          !_isPaused &&
          _currentDurationSeconds > 0) {
        // Plug just reconnected AND should be running.
        // Re-calculate startTime to resume from _currentDurationSeconds (which holds the paused time).
        final nowEpochSeconds = DateTime.now().millisecondsSinceEpoch ~/ 1000;
        _startTimeEpochSeconds = nowEpochSeconds; // Start from now
        // This will be used in _calculateAndStartCountdown to re-initiate the timer
        // based on the stored _currentDurationSeconds.

        // Propagate this new startTime back to Firebase to keep ESP aligned
        updateFirebase('startTime', nowEpochSeconds);
      }

      // Always re-calculate and manage the countdown after state changes
      _calculateAndStartCountdown();
    });
  }

  void _calculateAndStartCountdown() {
    _localCountdownTimer?.cancel();
    _localCountdownTimer = null;

    // --- Core Logic for Timer Operation ---
    // Timer should NOT run if:
    // 1. Plug is disconnected
    // 2. Plug is explicitly off (_isOn is false)
    // 3. No duration is set or timer has expired
    // 4. Timer is paused
    if (_currentPlugStatus == "Disconnected" ||
        !_isOn ||
        _currentDurationSeconds <= 0 ||
        _isPaused) {
      // If disconnected, paused, or off, the displayed time is the remaining duration.
      // If duration is 0, then timeLeft is 0.
      _calculatedTimeLeft = _currentDurationSeconds;
      if (_calculatedTimeLeft < 0) {
        _calculatedTimeLeft = 0; // Prevent negative display
      }
      return; // Do not start or continue local timer
    }

    // If we reach here, the plug should be running.
    // Calculate time left based on start time.
    if (_startTimeEpochSeconds != null) {
      final nowEpochSeconds = DateTime.now().millisecondsSinceEpoch ~/ 1000;
      final elapsedSeconds = nowEpochSeconds - _startTimeEpochSeconds!;

      _calculatedTimeLeft = _currentDurationSeconds - elapsedSeconds;

      if (_calculatedTimeLeft <= 0) {
        _calculatedTimeLeft = 0;
        // Timer has naturally expired, update Firebase to turn it off
        updateFirebase('durationSeconds', 0);
        updateFirebase('isPaused', false);
        updateFirebase('isOn', false); // Explicitly turn off
        updateFirebase('startTime', null);
      } else {
        // Start local countdown for smooth display
        _localCountdownTimer = Timer.periodic(const Duration(seconds: 1), (
          timer,
        ) {
          setState(() {
            _calculatedTimeLeft--;
            if (_calculatedTimeLeft <= 0) {
              _calculatedTimeLeft = 0;
              _localCountdownTimer?.cancel();
              _localCountdownTimer = null; // Clear reference
              // Timer finished: update Firebase
              updateFirebase('durationSeconds', 0);
              updateFirebase('isPaused', false);
              updateFirebase('isOn', false); // Explicitly turn off
              updateFirebase('startTime', null);
            }
          });
        });
      }
    } else {
      // Fallback for when startTime is null but it should be running (e.g., just turned on without a specific start time)
      // This case might need careful handling depending on ESP logic.
      // For now, it means timer cannot actively count down without a start time.
      _calculatedTimeLeft = _currentDurationSeconds;
    }
  }

  void updateFirebase(String key, dynamic value) {
    final uid = FirebaseAuth.instance.currentUser!.uid;
    FirebaseDatabase.instance
        .ref("users/$uid/plugs/${widget.plugId}/$key")
        .set(value);
  }

  void setTimer() {
    final totalSeconds = hours * 3600 + minutes * 60 + seconds;
    if (totalSeconds <= 0) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Please set a duration greater than 0.')),
      );
      return;
    }

    // When setting a new timer, it implies it's "On", not paused, and starts now
    updateFirebase('durationSeconds', totalSeconds);
    updateFirebase('isPaused', false);
    updateFirebase('isOn', true); // Set isOn to true when timer is set
    updateFirebase('startTime', DateTime.now().millisecondsSinceEpoch ~/ 1000);
    updateFirebase('lastUpdatedBy', 'app');
    updateFirebase('seenByEsp', false);

    // Immediately update local state for responsiveness
    setState(() {
      _currentDurationSeconds = totalSeconds;
      _isPaused = false;
      _isOn = true;
      _startTimeEpochSeconds = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    });
    _calculateAndStartCountdown(); // Recalculate and start local timer
  }

  void setPreset(int seconds) {
    final h = seconds ~/ 3600;
    final m = (seconds % 3600) ~/ 60;
    final s = seconds % 60;

    setState(() {
      hours = h;
      minutes = m;
      this.seconds = s;

      if (hourController.hasClients) hourController.jumpToItem(h);
      if (minuteController.hasClients) minuteController.jumpToItem(m);
      if (secondController.hasClients) secondController.jumpToItem(s);
    });
  }

  String formatTime(int s) {
    if (s < 0) s = 0;
    final h = (s ~/ 3600).toString().padLeft(2, '0');
    final m = ((s % 3600) ~/ 60).toString().padLeft(2, '0');
    final sec = (s % 60).toString().padLeft(2, '0');
    return "$h:$m:$sec";
  }

  void _showRenameDialog() {
    final controller = TextEditingController(text: _plugName);
    showDialog(
      context: context,
      builder:
          (_) => AlertDialog(
            title: const Text("Rename Plug"),
            content: TextField(
              controller: controller,
              decoration: const InputDecoration(hintText: "Enter new name"),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: const Text("Cancel"),
              ),
              ElevatedButton(
                onPressed: () {
                  final uid = FirebaseAuth.instance.currentUser!.uid;
                  FirebaseDatabase.instance
                      .ref("users/$uid/plugs/${widget.plugId}")
                      .update({'name': controller.text});
                  Navigator.pop(context);
                },
                child: const Text("Save"),
              ),
            ],
          ),
    );
  }

  void _deletePlug() async {
    final shouldDelete = await showDialog<bool>(
      context: context,
      builder:
          (context) => AlertDialog(
            title: const Text('Delete Plug?'),
            content: const Text(
              'Are you sure you want to delete this plug?\n'
              'It will enter setup mode if connected.',
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context, false),
                child: const Text('Cancel'),
              ),
              TextButton(
                onPressed: () => Navigator.pop(context, true),
                child: const Text('Delete'),
              ),
            ],
          ),
    );

    if (shouldDelete == true) {
      final uid = FirebaseAuth.instance.currentUser!.uid;
      final plugRef = FirebaseDatabase.instance.ref(
        "users/$uid/plugs/${widget.plugId}",
      );
      await plugRef.update({"isDeleted": true});
      Navigator.pop(context);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(_plugName),
        actions: [
          PopupMenuButton<String>(
            onSelected: (value) {
              if (value == 'rename') {
                _showRenameDialog();
              } else if (value == 'delete') {
                _deletePlug();
              }
            },
            itemBuilder:
                (context) => [
                  const PopupMenuItem(
                    value: 'rename',
                    child: Text('Rename Plug'),
                  ),
                  const PopupMenuItem(
                    value: 'delete',
                    child: Text('Remove Plug'),
                  ),
                ],
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                timePickerColumn(
                  "Hours",
                  23,
                  (v) => setState(() => hours = v),
                  hourController,
                ),
                timePickerColumn(
                  "Min",
                  59,
                  (v) => setState(() => minutes = v),
                  minuteController,
                ),
                timePickerColumn(
                  "Sec",
                  59,
                  (v) => setState(() => seconds = v),
                  secondController,
                ),
              ],
            ),
            const SizedBox(height: 30),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                presetButton("15m", 15 * 60),
                presetButton("30m", 30 * 60),
                presetButton("1h", 60 * 60),
              ],
            ),
            const SizedBox(height: 30),
            Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                ElevatedButton(
                  onPressed: setTimer,
                  child: const Text("Set Timer"),
                ),
                const SizedBox(height: 10),
                ElevatedButton(
                  onPressed: () {
                    // Update Firebase for pause
                    updateFirebase('isPaused', true);
                    // When pausing, the current _calculatedTimeLeft becomes the new duration.
                    // This is crucial for resuming from the exact point.
                    updateFirebase('durationSeconds', _calculatedTimeLeft);
                    updateFirebase('lastUpdatedBy', 'app');
                    updateFirebase('seenByEsp', false);
                    updateFirebase(
                      'startTime',
                      DateTime.now().millisecondsSinceEpoch ~/ 1000,
                    ); // Clear startTime
                  },

                  // Only allow pausing if the timer is currently 'Running'
                  // The button should be disabled otherwise.
                  child: const Text("Pause"),
                ),
                const SizedBox(height: 10),
                ElevatedButton(
                  onPressed: () {
                    // Update Firebase for resume
                    updateFirebase('isPaused', false);
                    updateFirebase(
                      'startTime',
                      DateTime.now().millisecondsSinceEpoch ~/ 1000,
                    ); // Set new start time
                    updateFirebase('lastUpdatedBy', 'app');
                    updateFirebase('seenByEsp', false);
                  },

                  // Only allow resuming if the timer is currently 'Paused'
                  child: const Text("Resume"),
                ),
                const SizedBox(height: 10),
                ElevatedButton(
                  onPressed: () {
                    // Reset timer in Firebase: duration, pause, isOn, startTime all go to initial/off state
                    updateFirebase('durationSeconds', 0);
                    updateFirebase('isPaused', false);
                    updateFirebase('isOn', false); // Explicitly turn off
                    updateFirebase(
                      'startTime',
                      DateTime.now().millisecondsSinceEpoch ~/ 1000,
                    );
                    updateFirebase('lastUpdatedBy', 'app');
                    updateFirebase('seenByEsp', false);
                  },
                  child: const Text("Reset"),
                ),
              ],
            ),
            const SizedBox(height: 30),
            Text("Time Remaining: ${formatTime(_calculatedTimeLeft)}"),
            LinearProgressIndicator(
              value:
                  _currentDurationSeconds > 0
                      ? _calculatedTimeLeft / _currentDurationSeconds
                      : 0,
              minHeight: 12,
            ),
            Text("Status: ${_currentPlugStatus}"), // Display calculated status
          ],
        ),
      ),
    );
  }

  Widget timePickerColumn(
    String label,
    int max,
    Function(int) onChanged,
    FixedExtentScrollController controller,
  ) {
    return Column(
      children: [
        Text(label),
        SizedBox(
          height: 100,
          width: 60,
          child: ListWheelScrollView.useDelegate(
            controller: controller,
            itemExtent: 40,
            onSelectedItemChanged: onChanged,
            perspective: 0.005,
            physics: const FixedExtentScrollPhysics(),
            childDelegate: ListWheelChildBuilderDelegate(
              childCount: max + 1,
              builder:
                  (context, index) => Center(child: Text(index.toString())),
            ),
          ),
        ),
      ],
    );
  }

  Widget presetButton(String label, int seconds) {
    return ElevatedButton(
      onPressed: () => setPreset(seconds),
      child: Text(label),
    );
  }

  @override
  void dispose() {
    hourController.dispose();
    minuteController.dispose();
    secondController.dispose();
    _firebaseSubscription?.cancel();
    _localCountdownTimer?.cancel();
    super.dispose();
  }
}
