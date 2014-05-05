from flask import Flask
from flask import render_template
from flask import request, redirect
from random import randint

app = Flask(__name__)

app.config.from_object('config')

monitored_host = 'localhost:20000'

import urllib2
import collections
import json


def get_trees(html):
    tree_dict = json.loads(html, object_pairs_hook=collections.OrderedDict)
    return tree_dict['call_tree']['react_aggregator']


def get_page():
    try:
        response = urllib2.urlopen('http://{}/{}'.format(monitored_host, 'call_tree'))
        html = response.read()
        return html
    except urllib2.URLError:
        pass

trees = {}
actions_times = {}
actions_times_snapshots = {}
actions_trees = {}


def process_tree(tree, timestamp_actions_times):
    tree_id = tree['id']
    global trees
    if tree_id in trees:
        return

    trees[tree_id] = tree

    actions = []
    delta = tree['actions'][0]['start_time']
    main_action_name = tree['actions'][0]['name']
    actions_trees[main_action_name] = tree
    get_actions(tree, actions, delta, True)
    for action in actions:
        action_name = action['name']
        delta_time = action['endTime'] - action['startTime']
        if not action_name in timestamp_actions_times:
            timestamp_actions_times[action_name] = []
        timestamp_actions_times[action_name].append(delta_time)


def get_actions(tree, actions, delta, root):
    if not root:
        actions.append({"name": tree['name'],
                        "startTime": tree['start_time'] - delta,
                        "endTime": tree['stop_time'] - delta,
                        "color": "#%06x" % randint(0, 0xFFFFFF)})

    if 'actions' in tree:
        for action in tree['actions']:
            get_actions(action, actions, delta, False)


def render_tree(tree):
    actions = []
    delta = tree['actions'][0]['start_time']
    get_actions(tree, actions, delta, True)

    return render_template("tree.html", tree_id=tree['id'], div_name='tree' + tree['id'],
                           data_provider=json.dumps(actions))


quantiles = [
    (0.5, '50%'),
    (0.7, '75%'),
    (0.9, '90%'),
    (0.95, '95%'),
    (0.99, '99%')
]


def build_stacked_histogram(name, snapshots):
    histogram_json = []
    for snapshot in snapshots:
        measurement = {'timestamp': snapshot['timestamp']}
        size = len(snapshot['actions_times'])
        measurement['calls'] = size

        for quantile in quantiles:
            pos = int(size * quantile[0])
            value = snapshot['actions_times'][pos]
            measurement[quantile[1]] = value

        histogram_json.append(measurement)

    return render_template("stacked_histogram.html", title=name,
                           div_name="Stacked_histogram_" + name,
                           data_provider=json.dumps(histogram_json))


def render_stacked_histograms():
    rendered_histograms = []
    global actions_times_snapshots
    for (name, snapshots) in actions_times_snapshots.iteritems():
        rendered_histograms.append(build_stacked_histogram(name, snapshots))
    return rendered_histograms

import thread
import time


def update_trees(delay):
    while True:
        try:
            timestamp_actions_times = {}
            for tree in get_trees(get_page()):
                process_tree(tree, timestamp_actions_times)

            global actions_times

            # timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())
            # timestamp = time.strftime("%H:%M:%S", time.gmtime())
            timestamp = time.time() * 1000
            for (key, value) in timestamp_actions_times.iteritems():
                if not key in actions_times:
                    actions_times[key] = []
                actions_times[key] += value
                if not key in actions_times_snapshots:
                    actions_times_snapshots[key] = []
                actions_times_snapshots[key].append({"timestamp": timestamp, "actions_times": sorted(value)})
        except:
            print('Failed to download')
        time.sleep(delay)

thread.start_new_thread(update_trees, (5, ))

@app.route('/')
@app.route('/index')
def index():
    trees_content = render_template("trees_list.html", trees=[render_tree(tree) for tree in actions_trees.values()])

    stacked_histograms_content = render_template("stacked_histograms_list.html", histograms=render_stacked_histograms())

    return render_template("index.html", host=monitored_host,
                           trees_number=len(trees),
                           stacked_histograms_content=stacked_histograms_content,
                           trees_content=trees_content)

@app.route('/set_host', methods=['POST'])
def set_host():
    global monitored_host
    host = request.form['host']
    monitored_host = host
    return redirect('/')

if __name__ == '__main__':
    app.run()
